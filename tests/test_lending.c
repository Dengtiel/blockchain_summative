/* Unit test: the lending service queues signed, screened requests. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lending.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

static AppState s;

int main(void) {
    if (system("rm -rf /tmp/lbt_lending_test") != 0) { /* nothing to remove */ }
    crypto_init("/tmp/lbt_lending_test");
    app_state_init(&s, "/tmp/lbt_lending_test");
    chain_append(&s.chain, block_create_genesis(1000));
    registry_add_book(&s.registry, "B001", "Algorithms", "Cormen");
    registry_add_member(&s.registry, "M001", "Alice", "BSE");
    registry_add_member(&s.registry, "M002", "Bob", "BSE");
    copy_add(&s.copies, "B001", COND_NEW);
    lending_init(&s);

    LendResult r, r2;
    CHECK(lending_borrow(&s, "B999", "M001", 0, NULL, &r) == -1, "unknown book is rejected");
    CHECK(lending_borrow(&s, "B001", "M999", 0, NULL, &r) == -1, "unknown member is rejected");

    CHECK(lending_borrow(&s, "B001", "M001", 0, NULL, &r) == 0, "borrow is queued");
    CHECK(strcmp(r.request_id, "REQ000001") == 0, "first request id is REQ000001");
    CHECK(r.nonce == 0 && !r.suspicious, "first nonce is 0 and not suspicious");
    CHECK(s.pool.count == 1, "the pool holds one entry");
    CHECK(record_verify(&s.pool.entries[0].record), "the queued record is signed");
    CHECK(s.copies.copies[0].state == COPY_AVAILABLE, "inventory is unchanged until mined");

    CHECK(lending_borrow(&s, "B001", "M002", 0, NULL, &r2) == -1, "the only copy is already claimed by a pending borrow");
    CHECK(lending_reserve(&s, "B001", "M002", &r2) == 0, "reserve is allowed once no copy is free");
    CHECK(r2.nonce == 0, "Bob's first request has nonce 0");

    CHECK(lending_return(&s, "B001-C1", 0, COND_GOOD, NULL, &r2) == -1, "return of a copy that is not borrowed is rejected");
    CHECK(lending_renew(&s, "B001-C1", &r2) == -1, "renew of a copy that is not borrowed is rejected");

    CHECK(lending_scan_overdue(&s) == 0, "no overdue flags when nothing is borrowed");

    CHECK(lending_days_late(1000, 1000) == 0, "on the due date there are no days late");
    CHECK(lending_days_late(1000, 1001) == 1, "one second late counts as one day");
    CHECK(lending_days_late(1000, 1000 + 86400 * 3) == 3, "exactly three days late is three days");

    /* duplicate request id: stored under a derived id and flagged */
    CHECK(lending_borrow(&s, "B001", "M001", 0, "REQ000001", &r2) == -1 || r2.suspicious,
          "a replayed request id is rejected or flagged");

    /* transfers */
    s.meta.ledger_mode = LEDGER_MODE_ACCOUNT;
    CHECK(lending_submit_transfer(&s, "M001", "M002", 5, 1, 0, 0, 0, &r2) == -1, "transfer without balance is rejected");
    CHECK(lending_submit_transfer(&s, "M001", "M001", 1, 0, 0, 0, 0, &r2) == -1, "self transfer is rejected");

    FineSummary fs;
    lending_fine_summary(&s, "M001", &fs);
    CHECK(fs.outstanding == 0, "no fine outstanding for a clean member");
    CHECK(lending_settle_fine(&s, "M001", &r2) == -1, "settling a zero fine is rejected");

    app_state_free(&s);
    printf(failures ? "test_lending: %d FAILED\n" : "test_lending: all passed\n", failures);
    return failures ? 1 : 0;
}
