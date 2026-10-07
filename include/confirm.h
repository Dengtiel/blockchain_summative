#ifndef CONFIRM_H
#define CONFIRM_H

#include "persistence.h"

/* ============================================================
 * confirm.h
 * Turning pending requests into a block. This is the only place
 * where inventory, reservations, balances and fines change.
 *
 * confirm_mine_batch():
 *   1. takes the pool in priority order and picks up to batch_size
 *      PENDING entries (SUSPICIOUS ones are never picked),
 *   2. re-checks every pick against the current state: ECDSA
 *      signature and signer identity, the member's next nonce, the
 *      copy's state machine, balances and ledger nonces. A pick that
 *      fails is rejected and removed with a reason; a pick that is
 *      merely early (a later nonce, a transfer for the other ledger
 *      mode) waits in the pool,
 *   3. applies the effects of each accepted pick, credits the return
 *      bonuses (10 on time, 5 late), and offers a returned copy to
 *      the head of the title's reservation queue,
 *   4. seals the accepted lending records into one block, solves
 *      its proof-of-work at the configured difficulty, and appends it.
 *
 * The block reward and the transfer fees are reported in the result
 * but not paid here: the caller pays them to the solo miner, the
 * pool miners or the cloud renter with confirm_pay_reward().
 * ============================================================ */

#define DEFAULT_BATCH_SIZE 5
#define MAX_BATCH_SIZE     50
#define MAX_REJECTED       64

typedef struct {
    char request_id[REQUEST_ID_LEN];
    char reason[160];
} RejectedEntry;

typedef struct {
    int confirmed;              /* entries mined into the block */
    int records;                /* lending records sealed in the block */
    int transfers;              /* token transfers applied */
    int deferred;               /* left in the pool: not yet eligible */
    int offered;                /* returned copies handed to a waiting member */
    unsigned long block_index;
    char block_hash[HASH_HEX_LEN];
    unsigned long attempts;     /* hashes computed by the proof-of-work */
    int return_bonus;           /* credited to members; equals the block's token_reward */
    int fees;                   /* transfer fees collected */
    int block_reward;           /* configured reward for sealing */
    int total_reward;           /* block_reward + fees: what the miner(s) earn */
    char sealed_by[SEALER_ID_LEN];
    RejectedEntry rejected[MAX_REJECTED];
    int rejected_count;
    char err[200];
} MineResult;

/* Mines one block. Returns 0 if a block was appended, -1 if nothing could
 * be mined (out->err says why; out->rejected may still list removals). */
int confirm_mine_batch(AppState *state, const char *sealer_id, int batch_size, MineResult *out);

/* Credits `amount` freshly minted tokens to owner_id on the active ledger. */
int confirm_pay_reward(AppState *state, const char *owner_id, int amount);

#endif
