/* Unit test: account model -- balances, nonce replay protection, history list. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ledger_account.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

int main(void) {
    static AccountLedger l;
    memset(&l, 0, sizeof(l));
    char tx[HASH_HEX_LEN];

    CHECK(account_balance(&l, "M001") == 0 && account_next_nonce(&l, "M001") == 0, "a new account has balance 0 and nonce 0");
    CHECK(account_credit_reward(&l, "M001", 10, tx, 100) == 0 && account_balance(&l, "M001") == 10, "a reward credits the balance directly");
    account_credit_reward(&l, "M001", 20, tx, 101);

    CHECK(account_transfer(&l, "M001", "M002", 5, 1, 0, tx, 200) == 0, "transfer with the correct nonce succeeds");
    CHECK(account_balance(&l, "M001") == 24 && account_balance(&l, "M002") == 5, "sender pays amount + fee, recipient receives amount");
    CHECK(account_next_nonce(&l, "M001") == 1, "the sender's nonce increments after an outgoing transfer");

    CHECK(account_transfer(&l, "M001", "M002", 5, 1, 0, tx, 201) == -1, "reusing a nonce (replay) is rejected");
    CHECK(account_transfer(&l, "M001", "M002", 5, 1, 5, tx, 202) == -1, "a future nonce is rejected");
    CHECK(account_balance(&l, "M001") == 24 && account_next_nonce(&l, "M001") == 1, "rejected transfers change nothing");

    CHECK(account_transfer(&l, "M001", "M002", 100, 1, 1, tx, 203) == -1, "insufficient balance is rejected");
    CHECK(account_next_nonce(&l, "M001") == 1, "a rejected transfer does not consume the nonce");
    CHECK(account_transfer(&l, "M001", "M001", 1, 0, 1, tx, 204) == -1, "a transfer to oneself is rejected");
    CHECK(account_transfer(&l, "M001", "M002", 0, 1, 1, tx, 205) == -1 && account_transfer(&l, "M001", "M002", -4, 1, 1, tx, 205) == -1,
          "zero or negative amounts are rejected");
    CHECK(account_transfer(&l, "M001", "M002", 4, 0, 1, tx, 206) == 0 && account_next_nonce(&l, "M001") == 2,
          "the next sequential nonce works");

    CHECK(account_transfer(&l, "M002", "M003", 3, 1, 0, tx, 300) == 0 && account_next_nonce(&l, "M002") == 1,
          "nonces are tracked independently per account");

    TxHistoryNode list[10];
    size_t n = account_history_for(&l, "M001", list, 10);
    CHECK(n == 4, "M001's history lists two rewards and two transfers");
    CHECK(list[0].timestamp >= list[1].timestamp && strcmp(list[0].recipient_id, "M002") == 0 && list[0].amount == 4,
          "history is newest first");
    CHECK(account_history_for(&l, "M002", list, 10) == 3, "history includes transactions where the member is the recipient");
    CHECK(account_history_for(&l, "NOBODY", list, 10) == 0, "an unknown member has an empty history");

    const char *path = "/tmp/lbt_account_test.txt";
    CHECK(account_ledger_save(&l, path) == 0, "ledger saves");
    static AccountLedger loaded;
    memset(&loaded, 0, sizeof(loaded));
    CHECK(account_ledger_load(&loaded, path) == 0, "ledger loads back");
    CHECK(account_balance(&loaded, "M001") == 20 && account_next_nonce(&loaded, "M001") == 2, "balances and nonces survive a reload");
    n = account_history_for(&loaded, "M001", list, 10);
    CHECK(n == 4 && list[0].amount == 4, "history order survives a reload");
    CHECK(account_transfer(&loaded, "M001", "M002", 1, 0, 1, tx, 400) == -1, "a replayed nonce is still rejected after a reload");
    account_ledger_free(&loaded);
    account_ledger_free(&l);
    remove(path);

    printf(failures == 0 ? "test_ledger_account: all checks passed\n" : "test_ledger_account: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
