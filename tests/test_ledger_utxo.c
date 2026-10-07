/* Unit test: UTXO model -- inputs/outputs, fee, change, double-spend protection. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ledger_utxo.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

int main(void) {
    static UTXOLedger l;
    l.count = 0; l.tx_counter = 0;
    char tx[HASH_HEX_LEN], tx2[HASH_HEX_LEN];

    CHECK(utxo_balance(&l, "M001") == 0, "a member starts with a balance of 0");
    CHECK(utxo_credit_reward(&l, "M001", 10, tx) == 0 && utxo_balance(&l, "M001") == 10, "a reward creates an unspent output");
    CHECK(utxo_credit_reward(&l, "M001", 5, tx2) == 0 && strcmp(tx, tx2) != 0, "each reward gets a distinct transaction id");
    CHECK(utxo_balance(&l, "M001") == 15 && utxo_unspent_count(&l, "M001") == 2, "balance is the sum of unspent outputs");
    CHECK(utxo_credit_reward(&l, "M001", 0, tx) == -1, "a zero reward is refused");

    /* transfer 4 + fee 1 = 5: first output (10) is consumed, change 5 returns */
    CHECK(utxo_transfer(&l, "M001", "M002", 4, 1, tx) == 0, "transfer of 4 with fee 1 succeeds");
    CHECK(utxo_balance(&l, "M002") == 4, "recipient receives exactly the amount");
    CHECK(utxo_balance(&l, "M001") == 15 - 5, "sender pays amount plus the fee");
    CHECK(l.outputs[0].spent == 1, "the consumed input is marked spent");
    int has_change = 0;
    for (size_t i = 0; i < l.count; i++)
        if (strcmp(l.outputs[i].owner_id, "M001") == 0 && l.outputs[i].amount == 5 && !l.outputs[i].spent && i >= 2) has_change = 1;
    CHECK(has_change, "excess input value returns to the sender as a change output");

    /* double spend: the spent output cannot fund a second transfer */
    int before = utxo_balance(&l, "M001");
    CHECK(utxo_transfer(&l, "M001", "M003", 20, 1, tx) == -1, "insufficient inputs are rejected");
    CHECK(utxo_balance(&l, "M001") == before && utxo_balance(&l, "M003") == 0, "a rejected transfer leaves the ledger unchanged");
    CHECK(utxo_transfer(&l, "M001", "M003", before, 1, tx) == -1, "inputs must cover amount PLUS fee");
    CHECK(utxo_transfer(&l, "M001", "M003", before - 1, 1, tx) == 0 && utxo_balance(&l, "M001") == 0,
          "spending exactly the whole balance works");
    CHECK(utxo_transfer(&l, "M001", "M003", 1, 0, tx) == -1, "no output can be spent twice (double spend refused)");

    CHECK(utxo_transfer(&l, "M002", "M003", -5, 1, tx) == -1 && utxo_transfer(&l, "M002", "M003", 2, -1, tx) == -1,
          "negative amount or fee is refused");
    CHECK(utxo_transfer(&l, "NOBODY", "M003", 1, 1, tx) == -1, "an unknown sender has nothing to spend");

    const char *path = "/tmp/lbt_utxo_test.txt";
    CHECK(utxo_ledger_save(&l, path) == 0, "ledger saves");
    static UTXOLedger loaded;
    CHECK(utxo_ledger_load(&loaded, path) == 0 && loaded.count == l.count && loaded.tx_counter == l.tx_counter,
          "ledger loads back");
    CHECK(utxo_balance(&loaded, "M002") == 4 && utxo_balance(&loaded, "M003") == utxo_balance(&l, "M003"),
          "balances survive a reload");
    remove(path);

    printf(failures == 0 ? "test_ledger_utxo: all checks passed\n" : "test_ledger_utxo: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
