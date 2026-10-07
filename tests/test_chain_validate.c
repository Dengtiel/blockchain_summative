/* Unit test: chain validation catches every kind of tampering. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chain_validate.h"
#include "mining.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

static AppState s;

static Block *mine_block(AppState *st, const char *rid, const char *member, LendingAction action,
                         unsigned long nonce, int fine, time_t now) {
    LendingRecord *r = calloc(1, sizeof(LendingRecord));
    snprintf(r->record_id, RECORD_ID_LEN, "%s", rid);
    snprintf(r->book_id, BOOK_ID_LEN, "B001");
    snprintf(r->copy_id, COPY_ID_LEN, "B001-C1");
    snprintf(r->book_title, TITLE_LEN, "Algorithms");
    snprintf(r->member_id, MEMBER_ID_LEN, "%s", member);
    snprintf(r->member_name, NAME_LEN, "Alice");
    r->action = action;
    r->nonce = nonce;
    r->fine_amount = fine;
    record_sign(r, member);
    int reward = action == ACTION_RETURNED ? (fine > 0 ? RETURN_BONUS_LATE : RETURN_BONUS_ON_TIME) : 0;
    Block *b = block_create(st->chain.tail, r, 1, "MINER1", reward, GENESIS_HASH, now);
    mining_solve(b, 2);
    chain_append(&st->chain, b);
    return b;
}

int main(void) {
    if (system("rm -rf /tmp/lbt_validate_test") != 0) { /* nothing to remove */ }
    crypto_init("/tmp/lbt_validate_test");
    app_state_init(&s, "/tmp/lbt_validate_test");
    chain_append(&s.chain, block_create_genesis(1000));
    registry_add_book(&s.registry, "B001", "Algorithms", "Cormen");
    registry_add_member(&s.registry, "M001", "Alice", "BSE");
    copy_add(&s.copies, "B001", COND_NEW);

    CHECK(chain_validate(&s, 0) == 1, "a genesis-only chain is valid");
    mine_block(&s, "REQ000001", "M001", ACTION_BORROWED, 0, 0, 2000);
    mine_block(&s, "REQ000002", "M001", ACTION_RETURNED, 1, 0, 3000);
    mine_block(&s, "REQ000003", "M001", ACTION_BORROWED, 2, 0, 4000);
    CHECK(chain_validate(&s, 0) == 1, "a properly built chain is valid");

    Block *b1 = chain_get(&s.chain, 1), *b2 = chain_get(&s.chain, 2), *b3 = chain_get(&s.chain, 3);

    int saved = b2->records[0].fine_amount;
    b2->records[0].fine_amount = 40;
    CHECK(chain_validate(&s, 0) == 0, "altering a record is detected (merkle root and signature)");
    b2->records[0].fine_amount = saved;
    CHECK(chain_validate(&s, 0) == 1, "restoring the record makes the chain valid again");

    char keep[HASH_HEX_LEN];
    snprintf(keep, sizeof(keep), "%s", b1->hash);
    b1->hash[5] = (b1->hash[5] == 'a') ? 'b' : 'a';
    CHECK(chain_validate(&s, 0) == 0, "altering a stored hash is detected");
    snprintf(b1->hash, HASH_HEX_LEN, "%s", keep);

    snprintf(keep, sizeof(keep), "%s", b3->previous_hash);
    snprintf(b3->previous_hash, HASH_HEX_LEN, "%064d", 1);
    CHECK(chain_validate(&s, 0) == 0, "breaking a previous_hash link is detected");
    snprintf(b3->previous_hash, HASH_HEX_LEN, "%s", keep);

    snprintf(keep, sizeof(keep), "%s", b2->merkle_root);
    snprintf(b2->merkle_root, HASH_HEX_LEN, "%064d", 2);
    CHECK(chain_validate(&s, 0) == 0, "altering the merkle root is detected");
    snprintf(b2->merkle_root, HASH_HEX_LEN, "%s", keep);

    time_t ts = b2->timestamp;
    b2->timestamp = b1->timestamp;
    CHECK(chain_validate(&s, 0) == 0, "a non-increasing timestamp is detected");
    b2->timestamp = ts;

    int reward = b2->token_reward;
    b2->token_reward = 500;
    CHECK(chain_validate(&s, 0) == 0, "an inflated token_reward is detected");
    b2->token_reward = reward;

    b3->records[0].sig_len = 0;
    CHECK(chain_validate(&s, 0) == 0, "a missing signature is detected");
    record_sign(&b3->records[0], "M001");
    CHECK(chain_validate(&s, 0) == 1, "a properly re-signed record is accepted again");
    b3->records[0].signature[3] ^= 0x55;
    CHECK(chain_validate(&s, 0) == 0, "a corrupted signature is detected");
    record_sign(&b3->records[0], "M001");
    CHECK(chain_validate(&s, 0) == 1, "the chain is valid once the signature is restored");

    /* a valid chain again for the remaining semantic checks */
    app_state_free(&s);
    app_state_init(&s, "/tmp/lbt_validate_test");
    chain_append(&s.chain, block_create_genesis(1000));
    registry_add_book(&s.registry, "B001", "Algorithms", "Cormen");
    registry_add_member(&s.registry, "M001", "Alice", "BSE");
    copy_add(&s.copies, "B001", COND_NEW);

    mine_block(&s, "REQ000001", "M001", ACTION_BORROWED, 0, 0, 2000);
    mine_block(&s, "REQ000002", "M001", ACTION_RETURNED, 5, 0, 3000);   /* nonce gap */
    CHECK(chain_validate(&s, 0) == 0, "a gap in a member's nonce sequence is detected (replay protection)");

    app_state_free(&s);
    app_state_init(&s, "/tmp/lbt_validate_test");
    chain_append(&s.chain, block_create_genesis(1000));
    registry_add_book(&s.registry, "B001", "Algorithms", "Cormen");
    copy_add(&s.copies, "B001", COND_NEW);
    mine_block(&s, "REQ000001", "M001", ACTION_BORROWED, 0, 0, 2000);
    CHECK(chain_validate(&s, 0) == 0, "a record for an unregistered member violates referential integrity");

    app_state_free(&s);
    app_state_init(&s, "/tmp/lbt_validate_test");
    chain_append(&s.chain, block_create_genesis(1000));
    registry_add_book(&s.registry, "B001", "Algorithms", "Cormen");
    registry_add_member(&s.registry, "M001", "Alice", "BSE");
    registry_add_member(&s.registry, "M002", "Bob", "BIT");
    copy_add(&s.copies, "B001", COND_NEW);
    Block *forged = mine_block(&s, "REQ000001", "M001", ACTION_BORROWED, 0, 0, 2000);
    snprintf(forged->records[0].member_id, MEMBER_ID_LEN, "M002");
    CHECK(chain_validate(&s, 0) == 0, "a record attributed to a different member is detected");
    snprintf(forged->records[0].member_id, MEMBER_ID_LEN, "M001");
    CHECK(chain_validate(&s, 0) == 1, "and is valid again once restored");

    CHECK(chain_tamper_demo(&s, TAMPER_RECORD) == 0, "the record tamper demo runs");
    CHECK(chain_validate(&s, 0) == 1, "the tamper demo reverts everything it changed");
    CHECK(chain_tamper_demo(&s, TAMPER_BLOCK) == 0 && chain_validate(&s, 0) == 1, "the block tamper demo runs and reverts");
    CHECK(chain_tamper_demo(&s, TAMPER_LINK) == 0 && chain_validate(&s, 0) == 1, "the link tamper demo runs and reverts");

    app_state_free(&s);
    if (system("rm -rf /tmp/lbt_validate_test") != 0) { /* best effort */ }
    printf(failures == 0 ? "test_chain_validate: all checks passed\n" : "test_chain_validate: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
