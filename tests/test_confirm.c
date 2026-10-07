/* Unit test: mining a batch applies effects, rewards and rejections correctly. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "confirm.h"
#include "chain_validate.h"
#include "lending.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

static AppState s;

int main(void) {
    if (system("rm -rf /tmp/lbt_confirm_test") != 0) { /* nothing to remove */ }
    crypto_init("/tmp/lbt_confirm_test");
    app_state_init(&s, "/tmp/lbt_confirm_test");
    chain_append(&s.chain, block_create_genesis(1000));
    registry_add_book(&s.registry, "B001", "Algorithms", "Cormen");
    registry_add_member(&s.registry, "M001", "Alice", "BSE");
    registry_add_member(&s.registry, "M002", "Bob", "BSE");
    registry_add_member(&s.registry, "M003", "Carol", "BSE");
    registry_add_member(&s.registry, "M004", "Dan", "BSE");
    registry_add_member(&s.registry, "M005", "Eve", "BSE");
    copy_add(&s.copies, "B001", COND_NEW);
    lending_init(&s);

    LendResult r;
    MineResult m;

    CHECK(confirm_mine_batch(&s, "MINER1", 5, &m) == -1, "an empty pool mines nothing");

    /* borrow, reserve, return -> offered, borrow by the reserver */
    lending_borrow(&s, "B001", "M001", 0, NULL, &r);
    CHECK(confirm_mine_batch(&s, "MINER1", 5, &m) == 0 && m.records == 1, "a borrow is mined into a block");
    CHECK(s.chain.length == 2 && s.copies.copies[0].state == COPY_BORROWED, "inventory changes only when mined");
    CHECK(strncmp(m.block_hash, "00", 2) == 0 && m.attempts >= 1, "the block carries real proof-of-work");
    CHECK(m.total_reward == s.meta.block_reward && m.fees == 0, "the block reward is reported, not yet paid");
    CHECK(chain_validate(&s, 0) == 1, "the chain validates after the first block");

    CHECK(lending_reserve(&s, "B001", "M002", &r) == 0, "Bob reserves the borrowed title");
    CHECK(confirm_mine_batch(&s, "MINER1", 5, &m) == 0 && reservation_has_member(&s.reservations, "B001", "M002"),
          "the reservation joins the queue when mined");

    CHECK(lending_return(&s, "B001-C1", 1, COND_GOOD, NULL, &r) == 0, "Alice's on-time return is queued");
    CHECK(confirm_mine_batch(&s, "MINER1", 5, &m) == 0 && m.offered == 1, "the returned copy is offered to the queue");
    CHECK(s.copies.copies[0].state == COPY_RESERVED && s.copies.copies[0].condition == COND_GOOD,
          "the copy is RESERVED and its reported condition is recorded");
    CHECK(m.return_bonus == RETURN_BONUS_ON_TIME && utxo_balance(&s.utxo_ledger, "M001") == RETURN_BONUS_ON_TIME,
          "an on-time return earns 10 tokens");
    CHECK(s.chain.tail->token_reward == RETURN_BONUS_ON_TIME, "the block header records the return bonus");

    CHECK(lending_borrow(&s, "B001", "M001", 0, NULL, &r) == -1, "Alice cannot take the copy held for Bob");
    CHECK(lending_borrow(&s, "B001", "M002", 0, NULL, &r) == 0, "Bob can borrow the offered copy");
    CHECK(confirm_mine_batch(&s, "MINER1", 5, &m) == 0 && reservation_peek_front(&s.reservations, "B001") == NULL,
          "his reservation is consumed");
    CHECK(s.copies.copies[0].state == COPY_BORROWED && strcmp(s.copies.copies[0].current_holder_id, "M002") == 0,
          "the copy is now Bob's");

    /* token transfer with a fee, paid to the miner */
    CHECK(lending_submit_transfer(&s, "M001", "M002", 3, 1, 0, 0, 0, &r) == 0, "a transfer is queued");
    CHECK(utxo_balance(&s.utxo_ledger, "M002") == 0, "balances are untouched until mined");
    CHECK(confirm_mine_batch(&s, "MINER1", 5, &m) == 0 && m.transfers == 1 && m.records == 0, "a transfer-only block is mined");
    CHECK(utxo_balance(&s.utxo_ledger, "M001") == 6 && utxo_balance(&s.utxo_ledger, "M002") == 3, "balances moved (fee deducted)");
    CHECK(m.fees == 1 && m.total_reward == s.meta.block_reward + 1, "the fee is added to the miner's reward");
    CHECK(confirm_pay_reward(&s, "MINER1", m.total_reward) == 0 && utxo_balance(&s.utxo_ledger, "MINER1") == m.total_reward,
          "the miner is paid on request");

    /* late return, fine, settlement */
    s.meta.clock_offset = 20L * 86400;
    CHECK(lending_return(&s, "B001-C1", 0, COND_GOOD, NULL, &r) == 0 && r.late && r.fine > 0, "a late return is assessed a fine");
    int fine = r.fine;
    CHECK(confirm_mine_batch(&s, "MINER1", 5, &m) == 0 && m.return_bonus == RETURN_BONUS_LATE, "a late return earns 5 tokens");
    FineSummary fs;
    lending_fine_summary(&s, "M002", &fs);
    CHECK(fs.assessed == fine && fs.outstanding == fine, "the fine is read from the chain");
    CHECK(lending_settle_fine(&s, "M002", &r) == 0, "settlement is queued");
    CHECK(confirm_mine_batch(&s, "MINER1", 5, &m) == 0, "the settlement is mined");
    lending_fine_summary(&s, "M002", &fs);
    CHECK(fs.outstanding == 0 && fs.settled == fine && utxo_balance(&s.utxo_ledger, LIBRARY_ACCOUNT) == fine,
          "the fine is settled and credited to the library");
    CHECK(chain_validate(&s, 0) == 1, "the chain still validates");

    /* suspicious entries are never mined */
    copy_add(&s.copies, "B001", COND_NEW);
    lending_borrow(&s, "B001", "M001", 0, NULL, &r);
    LendResult dup;
    lending_borrow(&s, "B001", "M001", 0, r.request_id, &dup);   /* replayed id: flagged */
    CHECK(dup.suspicious, "a replayed request id is flagged");
    CHECK(confirm_mine_batch(&s, "MINER1", 5, &m) == 0 && m.records == 1, "only the clean request is mined");
    CHECK(confirm_mine_batch(&s, "MINER1", 5, &m) == -1 && s.pool.count == 1, "the suspicious request stays in the pool");

    /* a tampered request is rejected and removed */
    pending_pool_remove(&s.pool, s.pool.entries[0].request_id);
    lending_renumber_pending(&s, "M001");
    CHECK(lending_return(&s, "B001-C1", 0, COND_GOOD, NULL, &r) == 0, "Alice returns the copy");
    CHECK(confirm_mine_batch(&s, "MINER1", 5, &m) == 0, "mined");
    CHECK(lending_borrow(&s, "B001", "M003", 0, NULL, &r) == 0, "Carol borrows");
    pending_pool_find(&s.pool, r.request_id)->record.signature[2] ^= 0x33;
    unsigned long len = s.chain.length;
    CHECK(confirm_mine_batch(&s, "MINER1", 5, &m) == -1 && m.rejected_count == 1 && s.pool.count == 0 && s.chain.length == len,
          "a request with a corrupted signature is rejected and removed");

    /* batch size and ordering */
    copy_add(&s.copies, "B001", COND_NEW);
    copy_add(&s.copies, "B001", COND_NEW);
    s.meta.clock_offset = 0;
    lending_borrow(&s, "B001", "M004", 0, NULL, &r);
    lending_borrow(&s, "B001", "M005", 0, NULL, &r);
    CHECK(confirm_mine_batch(&s, "MINER1", 1, &m) == 0 && m.records == 1 && s.pool.count == 1, "batch size limits the block");
    CHECK(confirm_mine_batch(&s, "MINER1", 1, &m) == 0 && s.pool.count == 0, "the next block takes the rest");
    CHECK(chain_validate(&s, 0) == 1, "the chain validates after every kind of block");

    /* account model: nonce-protected transfers */
    s.meta.ledger_mode = LEDGER_MODE_ACCOUNT;
    confirm_pay_reward(&s, "M001", 20);
    CHECK(lending_submit_transfer(&s, "M001", "M002", 5, 1, 1, 7, 0, &r) == -1, "a wrong explicit nonce is refused at submission");
    CHECK(lending_submit_transfer(&s, "M001", "M002", 5, 1, 1, 0, 0, &r) == 0 &&
          lending_submit_transfer(&s, "M001", "M002", 4, 1, 0, 0, 0, &r) == 0, "two transfers queue with nonces 0 and 1");
    CHECK(confirm_mine_batch(&s, "MINER1", 5, &m) == 0 && m.transfers == 2, "both are mined in order");
    CHECK(account_balance(&s.account_ledger, "M001") == 9 && account_next_nonce(&s.account_ledger, "M001") == 2,
          "balances and the sender's nonce advance");

    app_state_free(&s);
    if (system("rm -rf /tmp/lbt_confirm_test") != 0) { /* best effort */ }
    printf(failures ? "test_confirm: %d FAILED\n" : "test_confirm: all checks passed\n", failures);
    return failures ? 1 : 0;
}
