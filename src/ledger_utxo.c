/* ============================================================
 * ledger_utxo.c
 * UTXO-based token model: balances are derived, never stored.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ledger_utxo.h"
#include "ledger_common.h"
#include "input.h"

int utxo_ledger_load(UTXOLedger *ledger, const char *path) {
    ledger->count = 0;
    ledger->tx_counter = 0;

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[256];
    if (fgets(line, sizeof(line), f)) ledger->tx_counter = strtoul(line, NULL, 10);

    while (fgets(line, sizeof(line), f) && ledger->count < MAX_UTXOS) {
        input_trim_newline(line);
        if (line[0] == '\0') continue;

        char *fld[4];
        if (input_split_pipe(line, fld, 4) != 4) continue;
        if (strlen(fld[0]) != HASH_HEX_LEN - 1 || !input_valid_id(fld[1], MEMBER_ID_LEN)) continue;

        UTXO *u = &ledger->outputs[ledger->count++];
        snprintf(u->tx_id, HASH_HEX_LEN, "%s", fld[0]);
        snprintf(u->owner_id, MEMBER_ID_LEN, "%s", fld[1]);
        u->amount = atoi(fld[2]);
        u->spent = atoi(fld[3]) != 0;
    }
    fclose(f);
    return 0;
}

int utxo_ledger_save(const UTXOLedger *ledger, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "%lu\n", ledger->tx_counter);
    for (size_t i = 0; i < ledger->count; i++) {
        const UTXO *u = &ledger->outputs[i];
        fprintf(f, "%s|%s|%d|%d\n", u->tx_id, u->owner_id, u->amount, u->spent);
    }
    fclose(f);
    return 0;
}

static UTXO *append_output(UTXOLedger *ledger, const char *tx_id, const char *owner_id, int amount) {
    if (ledger->count >= MAX_UTXOS) return NULL;
    UTXO *u = &ledger->outputs[ledger->count++];
    snprintf(u->tx_id, HASH_HEX_LEN, "%s", tx_id);
    snprintf(u->owner_id, MEMBER_ID_LEN, "%s", owner_id);
    u->amount = amount;
    u->spent = 0;
    return u;
}

int utxo_credit_reward(UTXOLedger *ledger, const char *owner_id, int amount,
                       char out_tx_id[HASH_HEX_LEN]) {
    if (amount <= 0) return -1;

    char seed[128];
    snprintf(seed, sizeof(seed), "REWARD:%s:%d", owner_id, amount);
    tx_generate_id(seed, ledger->tx_counter++, out_tx_id);
    return append_output(ledger, out_tx_id, owner_id, amount) ? 0 : -1;
}

int utxo_balance(const UTXOLedger *ledger, const char *owner_id) {
    int total = 0;
    for (size_t i = 0; i < ledger->count; i++) {
        if (!ledger->outputs[i].spent && strcmp(ledger->outputs[i].owner_id, owner_id) == 0) {
            total += ledger->outputs[i].amount;
        }
    }
    return total;
}

int utxo_unspent_count(const UTXOLedger *ledger, const char *owner_id) {
    int n = 0;
    for (size_t i = 0; i < ledger->count; i++) {
        if (!ledger->outputs[i].spent && strcmp(ledger->outputs[i].owner_id, owner_id) == 0) n++;
    }
    return n;
}

int utxo_transfer(UTXOLedger *ledger, const char *sender_id, const char *recipient_id,
                  int amount, int fee, char out_tx_id[HASH_HEX_LEN]) {
    if (amount <= 0 || fee < 0) return -1;

    int need = amount + fee;
    int collected = 0;
    size_t chosen[MAX_UTXOS];
    size_t chosen_count = 0;

    /* select inputs: whole unspent outputs until they cover amount + fee */
    for (size_t i = 0; i < ledger->count && collected < need; i++) {
        UTXO *u = &ledger->outputs[i];
        if (!u->spent && strcmp(u->owner_id, sender_id) == 0) {
            chosen[chosen_count++] = i;
            collected += u->amount;
        }
    }
    if (collected < need) return -1;

    int change = collected - need;
    size_t new_outputs = 1 + (change > 0 ? 1 : 0);
    if (ledger->count + new_outputs > MAX_UTXOS) return -1;

    /* mark the inputs spent -- an output can never be consumed twice */
    for (size_t k = 0; k < chosen_count; k++) ledger->outputs[chosen[k]].spent = 1;

    char seed[160];
    snprintf(seed, sizeof(seed), "XFER:%s:%s:%d:%d", sender_id, recipient_id, amount, fee);
    tx_generate_id(seed, ledger->tx_counter++, out_tx_id);
    append_output(ledger, out_tx_id, recipient_id, amount);

    if (change > 0) append_output(ledger, out_tx_id, sender_id, change); /* change returns to the sender */
    return 0;
}
