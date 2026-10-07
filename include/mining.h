#ifndef MINING_H
#define MINING_H

#include "blockchain.h"

/* ============================================================
 * mining.h
 * Real proof-of-work. mining_solve() genuinely increments a block's
 * pow_nonce and recomputes its SHA-256 hash until the hash starts
 * with `difficulty` hexadecimal zeros -- nothing is faked, and the
 * number of attempts it reports is the number of hashes computed.
 *
 *   solo  : one miner solves the block and takes the whole reward.
 *   pool  : several simulated miners, each with a random hash rate
 *           (attempts per round), share the reward in proportion to
 *           the attempts they contributed, after a pool fee.
 *   cloud : the caller rents mining power for 1-5 rounds, paying a
 *           fixed fee per round (handled by the CLI layer).
 * ============================================================ */

#define MIN_DIFFICULTY         1
#define MAX_DIFFICULTY         4
#define DEFAULT_DIFFICULTY     2
#define MINING_POOL_FEE_PERCENT 2
#define MAX_POOL_MINERS        16

/* True if hash_hex starts with at least `difficulty` '0' characters. */
int mining_hash_meets_difficulty(const char *hash_hex, int difficulty);

/* Increments block->pow_nonce from zero until the block hash meets the
 * difficulty, stores the winning hash in block->hash, and returns the
 * number of hash attempts made (always >= 1). */
unsigned long mining_solve(Block *block, int difficulty);

typedef struct {
    char miner_id[24];
    unsigned long hash_rate;    /* attempts this miner performs per round */
    unsigned long attempts;     /* attempts credited to it for the solved block */
    double share_pct;           /* attempts / total_attempts * 100 */
    int reward;                 /* tokens paid to this miner */
} PoolMiner;

/* Distributes the work and the reward of one solved block across
 * `num_miners` simulated miners (named POOLMINER1..n).
 *
 *  1. Each miner gets a random hash rate (50-200 attempts per round).
 *  2. Rounds run until the miners' combined attempts reach
 *     total_attempts, so each miner's attempts are proportional to its
 *     hash rate (rounding leftovers go to the fastest miner).
 *  3. The pool takes pool_fee_percent of total_reward, and the rest is
 *     split as (miner_attempts / total_attempts) * distributable.
 *
 * Returns the number of rounds simulated and writes the pool's fee to
 * *pool_fee_out. miners[] must have room for num_miners entries. */
unsigned long mining_pool_distribute(unsigned long total_attempts, int num_miners,
                                     int pool_fee_percent, int total_reward,
                                     PoolMiner *miners, int *pool_fee_out);

#endif
