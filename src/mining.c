/* ============================================================
 * mining.c
 * Real proof-of-work solving, plus the pool-mining work/reward
 * distribution across several simulated miners.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mining.h"

#define MIN_HASH_RATE 50
#define MAX_HASH_RATE 200

int mining_hash_meets_difficulty(const char *hash_hex, int difficulty) {
    if (difficulty <= 0) return 1;
    for (int i = 0; i < difficulty; i++) {
        if (hash_hex[i] != '0') return 0;
    }
    return 1;
}

unsigned long mining_solve(Block *block, int difficulty) {
    unsigned long attempts = 0;
    char candidate[HASH_HEX_LEN];

    block->pow_nonce = 0;
    for (;;) {
        attempts++;
        block_compute_hash(block, candidate);
        if (mining_hash_meets_difficulty(candidate, difficulty)) break;
        block->pow_nonce++;
    }

    snprintf(block->hash, HASH_HEX_LEN, "%s", candidate);
    return attempts;
}

unsigned long mining_pool_distribute(unsigned long total_attempts, int num_miners,
                                     int pool_fee_percent, int total_reward,
                                     PoolMiner *miners, int *pool_fee_out) {
    if (num_miners <= 0 || total_attempts == 0) {
        if (pool_fee_out) *pool_fee_out = 0;
        return 0;
    }

    /* 1. random hash rates */
    unsigned long rate_sum = 0;
    int fastest = 0;
    for (int i = 0; i < num_miners; i++) {
        snprintf(miners[i].miner_id, sizeof(miners[i].miner_id), "POOLMINER%d", i + 1);
        miners[i].hash_rate = MIN_HASH_RATE + (unsigned long)(rand() % (MAX_HASH_RATE - MIN_HASH_RATE + 1));
        rate_sum += miners[i].hash_rate;
        if (miners[i].hash_rate > miners[fastest].hash_rate) fastest = i;
    }

    /* 2. rounds until the combined work covers the real attempt count */
    unsigned long full_rounds = total_attempts / rate_sum;
    unsigned long remainder = total_attempts % rate_sum;
    unsigned long assigned = 0;
    for (int i = 0; i < num_miners; i++) {
        miners[i].attempts = full_rounds * miners[i].hash_rate + (remainder * miners[i].hash_rate) / rate_sum;
        assigned += miners[i].attempts;
    }
    miners[fastest].attempts += total_attempts - assigned;

    /* 3. fee, then proportional payout */
    int fee = (total_reward * pool_fee_percent + 50) / 100;
    if (fee > total_reward) fee = total_reward;
    int distributable = total_reward - fee;
    int paid = 0;
    for (int i = 0; i < num_miners; i++) {
        miners[i].share_pct = 100.0 * (double)miners[i].attempts / (double)total_attempts;
        miners[i].reward = (int)((long long)distributable * (long long)miners[i].attempts / (long long)total_attempts);
        paid += miners[i].reward;
    }
    miners[fastest].reward += distributable - paid; /* integer-division leftover */

    if (pool_fee_out) *pool_fee_out = fee;
    return full_rounds + (remainder > 0 ? 1 : 0);
}
