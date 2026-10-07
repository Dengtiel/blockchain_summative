/* Unit test: real proof-of-work at each difficulty and pool-mining distribution. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blockchain.h"
#include "mining.h"

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
    srand(7);
    Block *g = block_create_genesis(1000);

    for (int d = MIN_DIFFICULTY; d <= MAX_DIFFICULTY - 1; d++) {
        Block *t = block_create(g, one_record("REQ0000XX"), 1, "MINER1", 0, GENESIS_HASH, 2000);
        unsigned long attempts = mining_solve(t, d);
        char recomputed[HASH_HEX_LEN];
        block_compute_hash(t, recomputed);
        char label[112];
        snprintf(label, sizeof(label), "difficulty %d: hash starts with %d zero(s) after %lu attempt(s)", d, d, attempts);
        CHECK(attempts >= 1 && strcmp(recomputed, t->hash) == 0 && mining_hash_meets_difficulty(t->hash, d), label);
        block_free(t);
    }

    Block *b = block_create(g, one_record("REQ000001"), 1, "MINER1", 0, GENESIS_HASH, 2000);
    mining_solve(b, 2);
    CHECK(strncmp(b->hash, "00", 2) == 0, "difficulty 2 yields a hash starting with 00");
    CHECK(!mining_hash_meets_difficulty("0a", 2) && mining_hash_meets_difficulty("00ab", 2),
          "difficulty predicate counts leading zeros");
    CHECK(mining_hash_meets_difficulty("ffff", 0), "difficulty 0 accepts any hash");

    unsigned long nonce_before = b->pow_nonce;
    b->pow_nonce++;
    char h[HASH_HEX_LEN];
    block_compute_hash(b, h);
    CHECK(strcmp(h, b->hash) != 0, "the winning nonce is specific to this block");
    b->pow_nonce = nonce_before;
    block_free(b);

    PoolMiner miners[8];
    int fee = 0;
    unsigned long rounds = mining_pool_distribute(1000, 4, 2, 50, miners, &fee);
    unsigned long attempt_sum = 0;
    int reward_sum = 0, rates_ok = 1;
    double pct = 0;
    for (int i = 0; i < 4; i++) {
        attempt_sum += miners[i].attempts;
        reward_sum += miners[i].reward;
        pct += miners[i].share_pct;
        if (miners[i].hash_rate < 50 || miners[i].hash_rate > 200) rates_ok = 0;
    }
    CHECK(rates_ok, "each simulated miner gets a hash rate between 50 and 200");
    CHECK(attempt_sum == 1000, "miner attempts sum to the real attempt count");
    CHECK(fee == 1 && reward_sum + fee == 50, "2% pool fee is taken and the rest is fully distributed");
    CHECK(rounds >= 1, "at least one round is simulated");
    CHECK(pct > 99.99 && pct < 100.01, "share percentages add up to 100");
    CHECK(strcmp(miners[0].miner_id, "POOLMINER1") == 0, "miners are named POOLMINER1..n");
    CHECK(mining_pool_distribute(0, 4, 2, 50, miners, &fee) == 0 && fee == 0, "zero attempts distributes nothing");

    block_free(g);
    printf(failures == 0 ? "test_mining: all checks passed\n" : "test_mining: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
