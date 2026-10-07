#ifndef LEDGER_UTXO_H
#define LEDGER_UTXO_H

#include <stddef.h>
#include "record.h"
#include "crypto_utils.h"

/* ============================================================
 * ledger_utxo.h
 * Unspent-transaction-output token model.
 *
 * A balance is never stored; it is the sum of an owner's unspent
 * outputs. A transfer consumes whole outputs (the inputs) until
 * they cover amount + fee, marks them spent so they cannot be used
 * again (no double spending), creates an output for the recipient,
 * and returns any excess to the sender as a new change output.
 * A transfer whose inputs cannot cover amount + fee is rejected and
 * leaves the ledger untouched.
 * ============================================================ */

#define MAX_UTXOS 2000

typedef struct {
    char tx_id[HASH_HEX_LEN];       /* the transaction that created this output */
    char owner_id[MEMBER_ID_LEN];
    int amount;
    int spent;
} UTXO;

typedef struct {
    UTXO outputs[MAX_UTXOS];
    size_t count;
    unsigned long tx_counter;
} UTXOLedger;

int utxo_ledger_load(UTXOLedger *ledger, const char *path);
int utxo_ledger_save(const UTXOLedger *ledger, const char *path);

/* Mints `amount` new tokens to owner_id as an unspent output (block
 * reward or return bonus). Returns -1 for a non-positive amount or a
 * full ledger. */
int utxo_credit_reward(UTXOLedger *ledger, const char *owner_id, int amount,
                       char out_tx_id[HASH_HEX_LEN]);

int utxo_balance(const UTXOLedger *ledger, const char *owner_id);

/* Number of unspent outputs owned by owner_id. */
int utxo_unspent_count(const UTXOLedger *ledger, const char *owner_id);

/* Transfers `amount` from sender to recipient, paying `fee` on top.
 * Returns 0 on success and -1 (ledger unchanged) if amount <= 0,
 * fee < 0, or the sender's unspent outputs total less than amount+fee. */
int utxo_transfer(UTXOLedger *ledger, const char *sender_id, const char *recipient_id,
                  int amount, int fee, char out_tx_id[HASH_HEX_LEN]);

#endif
