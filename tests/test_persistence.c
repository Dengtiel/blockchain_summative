/* Unit test: full-state save/load round trip and corrupt-file handling. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "persistence.h"
#include "mining.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

static AppState a, b;

int main(void) {
    const char *dir = "/tmp/lbt_persist_test";
    if (system("rm -rf /tmp/lbt_persist_test") != 0) { /* nothing to remove */ }
    mkdir(dir, 0755);
    crypto_init("/tmp/lbt_persist_test/keys");

    app_state_init(&a, dir);
    app_state_load(&a);
    CHECK(a.chain.length == 1 && a.chain.head->index == 0, "a fresh directory starts with a genesis block");
    CHECK(a.meta.difficulty == DEFAULT_DIFFICULTY && a.meta.block_reward == DEFAULT_BLOCK_REWARD &&
          a.meta.ledger_mode == LEDGER_MODE_UTXO, "defaults are applied when meta.txt is missing");

    /* build some state */
    registry_add_book(&a.registry, "B001", "Algorithms", "Cormen");
    registry_add_member(&a.registry, "M001", "Alice", "BSE");
    copy_add(&a.copies, "B001", COND_NEW);
    reservation_enqueue(&a.reservations, "B001", "M001", 777);

    LendingRecord *rec = calloc(1, sizeof(LendingRecord));
    snprintf(rec->record_id, RECORD_ID_LEN, "REQ000001");
    snprintf(rec->book_id, BOOK_ID_LEN, "B001");
    snprintf(rec->copy_id, COPY_ID_LEN, "B001-C1");
    snprintf(rec->member_id, MEMBER_ID_LEN, "M001");
    rec->action = ACTION_BORROWED;
    record_sign(rec, "M001");
    Block *blk = block_create(a.chain.tail, rec, 1, "MINER1", 10, GENESIS_HASH, 5000);
    mining_solve(blk, a.meta.difficulty);
    chain_append(&a.chain, blk);

    LendingRecord pr;
    memset(&pr, 0, sizeof(pr));
    snprintf(pr.member_id, MEMBER_ID_LEN, "M001");
    pending_pool_add_lending(&a.pool, "REQ000002", &pr, 1, 0, 6000);
    pending_pool_mark_suspicious(&a.pool, "REQ000002", "unit test");

    char tx[HASH_HEX_LEN];
    utxo_credit_reward(&a.utxo_ledger, "M001", 10, tx);
    account_credit_reward(&a.account_ledger, "M001", 10, tx, 100);
    account_transfer(&a.account_ledger, "M001", "M002", 3, 1, 0, tx, 101);
    fines_add_settled(&a.fines, "M001", 4);
    a.meta.difficulty = 3;
    a.meta.ledger_mode = LEDGER_MODE_ACCOUNT;
    a.meta.clock_offset = 86400;
    a.meta.max_renewals = 5;

    CHECK(app_state_save(&a) == 0, "full state saves");

    app_state_init(&b, dir);
    CHECK(app_state_load(&b) == 0, "full state loads with no problems");
    CHECK(b.chain.length == 2 && strcmp(b.chain.tail->hash, a.chain.tail->hash) == 0, "chain round-trips with identical hashes");
    CHECK(b.chain.tail->record_count == 1 && strcmp(b.chain.tail->records[0].record_id, "REQ000001") == 0 &&
          b.chain.tail->records[0].sig_len == a.chain.tail->records[0].sig_len, "block records and signatures round-trip");
    CHECK(record_verify(&b.chain.tail->records[0]) == 1, "a reloaded record still verifies");
    CHECK(strcmp(b.chain.tail->previous_hash, b.chain.head->hash) == 0, "hash linkage survives a reload");
    CHECK(b.registry.book_count == 1 && b.registry.member_count == 1, "registries round-trip");
    CHECK(b.copies.count == 1 && reservation_peek_front(&b.reservations, "B001") != NULL, "copies and reservations round-trip");
    CHECK(b.pool.count == 1 && b.pool.entries[0].status == POOL_SUSPICIOUS, "the pending pool round-trips with statuses");
    CHECK(utxo_balance(&b.utxo_ledger, "M001") == 10, "the UTXO set round-trips");
    CHECK(account_balance(&b.account_ledger, "M001") == 6 && account_next_nonce(&b.account_ledger, "M001") == 1,
          "account balances and nonces round-trip");
    CHECK(fines_settled(&b.fines, "M001") == 4, "settled fines round-trip");
    CHECK(b.meta.difficulty == 3 && b.meta.ledger_mode == LEDGER_MODE_ACCOUNT && b.meta.clock_offset == 86400 &&
          b.meta.max_renewals == 5 && b.meta.block_reward == DEFAULT_BLOCK_REWARD, "meta settings round-trip");
    CHECK(app_now(&b) >= time(NULL) + 86399, "the clock offset moves app_now forward");

    /* corrupt chain file must be detected and protected */
    app_state_free(&b);
    FILE *f = fopen("/tmp/lbt_persist_test/chain.dat", "wb");
    fputs("this is not a chain", f);
    fclose(f);
    app_state_init(&b, dir);
    fprintf(stderr, "-- expected corruption error follows --\n");
    CHECK(app_state_load(&b) >= 1 && b.chain_corrupt == 1, "a corrupt chain.dat is detected");
    app_state_save(&b);
    f = fopen("/tmp/lbt_persist_test/chain.dat", "rb");
    char buf[32] = {0};
    size_t n = fread(buf, 1, 19, f);
    fclose(f);
    CHECK(n == 19 && strcmp(buf, "this is not a chain") == 0, "a corrupt chain file is never overwritten on save");

    app_state_free(&a);
    app_state_free(&b);
    if (system("rm -rf /tmp/lbt_persist_test") != 0) { /* best effort */ }
    printf(failures == 0 ? "test_persistence: all checks passed\n" : "test_persistence: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
