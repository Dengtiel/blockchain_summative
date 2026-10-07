/* ============================================================
 * ledger_common.c
 * Transaction-id generation and mode names shared by both ledgers.
 * ============================================================ */

#include <stdio.h>
#include "ledger_common.h"

const char *ledger_mode_name(LedgerMode mode) {
    return mode == LEDGER_MODE_UTXO ? "UTXO" : "ACCOUNT";
}

void tx_generate_id(const char *seed, unsigned long counter, char out_hex[HASH_HEX_LEN]) {
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "%s|%lu", seed, counter);
    if (len < 0) len = 0;
    if ((size_t)len >= sizeof(buf)) len = sizeof(buf) - 1;
    sha256_hex((const unsigned char *)buf, (size_t)len, out_hex);
}
