/* Unit test: block construction, hash linking, strictly increasing
 * timestamps and the chain list. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blockchain.h"
#include "merkle.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

static LendingRecord *one_record(const char *id) {
    LendingRecord *r = calloc(1, sizeof(LendingRecord));
    snprintf(r->record_id, RECORD_ID_LEN, "%s", id);
    snprintf(r->member_id, MEMBER_ID_LEN, "M001");
    r->action = ACTION_BORROWED;
    return r;
}

int main(void) {
    Chain chain;
    chain_init(&chain);

    Block *g = block_create_genesis(1000);
    chain_append(&chain, g);
    CHECK(g->index == 0 && g->record_count == 0, "genesis has index 0 and no records");
    CHECK(strcmp(g->previous_hash, GENESIS_HASH) == 0, "genesis previous_hash is all zeros");
    char again[HASH_HEX_LEN];
    block_compute_hash(g, again);
    CHECK(strcmp(again, g->hash) == 0, "genesis stored hash matches a recompute");

    Block *b1 = block_create(g, one_record("REQ000001"), 1, "MINER1", 10, GENESIS_HASH, 1000);
    CHECK(b1->index == 1 && b1->record_count == 1, "new block has the next index and its record count");
    CHECK(b1->timestamp == 1001, "same-second block is bumped to previous timestamp + 1");
    CHECK(strcmp(b1->previous_hash, g->hash) == 0, "block links to the previous hash");
    CHECK(b1->timestamp > g->timestamp, "timestamp is strictly greater than the parent's");
    CHECK(strlen(b1->merkle_root) == 64, "merkle root is computed at creation");

    Block *later = block_create(g, one_record("REQ000009"), 1, "MINER1", 0, GENESIS_HASH, 5000);
    CHECK(later->timestamp == 5000, "a later wall-clock time is used as is");
    block_free(later);

    block_compute_hash(b1, b1->hash);
    char saved[HASH_HEX_LEN];
    snprintf(saved, sizeof(saved), "%s", b1->hash);
    b1->token_reward = 999;
    block_compute_hash(b1, again);
    CHECK(strcmp(again, saved) != 0, "changing a header field changes the block hash");
    b1->token_reward = 10;
    b1->records[0].fine_amount = 5;
    merkle_compute_root(b1->records, b1->record_count, again);
    CHECK(strcmp(again, b1->merkle_root) != 0, "changing a record no longer matches the stored merkle root");
    b1->records[0].fine_amount = 0;

    chain_append(&chain, b1);
    CHECK(chain.length == 2 && chain.tail == b1 && g->next == b1, "chain links blocks in order");
    CHECK(chain_get(&chain, 1) == b1 && chain_get(&chain, 5) == NULL, "chain_get finds blocks by index");

    chain_free(&chain);
    CHECK(chain.length == 0 && chain.head == NULL, "chain_free empties the chain");

    printf(failures == 0 ? "test_blockchain: all checks passed\n" : "test_blockchain: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
