#ifndef LEDGER_COMMON_H
#define LEDGER_COMMON_H

#include <stddef.h>
#include "crypto_utils.h"

/* ============================================================
 * ledger_common.h
 * Shared pieces of the two interchangeable token-balance models
 * (UTXO and account-based): the mode selector, the fixed
 * transaction fee, and one transaction-id generator so ids look
 * the same whichever model produced them.
 * ============================================================ */

#define TX_FEE          1     /* fixed fee on every member-initiated transfer */

typedef enum {
    LEDGER_MODE_UTXO,
    LEDGER_MODE_ACCOUNT
} LedgerMode;

const char *ledger_mode_name(LedgerMode mode);

/* Builds a transaction id as SHA-256("<seed>|<counter>"). The counter
 * is a per-ledger monotonic value (not wall time), so two transactions
 * created in the same second still get different ids. */
void tx_generate_id(const char *seed, unsigned long counter, char out_hex[HASH_HEX_LEN]);

#endif
