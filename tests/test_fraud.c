/* Unit test: the four misuse / fraud heuristics. */
#include <stdio.h>
#include <string.h>
#include "fraud.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

int main(void) {
    /* 1. excessive concurrent borrows */
    CHECK(!fraud_check_excessive_concurrent_borrows(2, 3).triggered, "2 held with a limit of 3: allowed");
    CHECK(fraud_check_excessive_concurrent_borrows(3, 3).triggered, "3 held with a limit of 3: the next borrow is flagged");
    CHECK(fraud_check_excessive_concurrent_borrows(5, 3).triggered, "above the limit is flagged");

    /* 2. rapid re-borrow */
    CHECK(fraud_check_rapid_reborrow(60, RAPID_REBORROW_WINDOW_SECONDS).triggered, "re-borrow one minute after return is flagged");
    CHECK(fraud_check_rapid_reborrow(RAPID_REBORROW_WINDOW_SECONDS - 1, RAPID_REBORROW_WINDOW_SECONDS).triggered,
          "re-borrow just inside 24 hours is flagged");
    CHECK(!fraud_check_rapid_reborrow(RAPID_REBORROW_WINDOW_SECONDS, RAPID_REBORROW_WINDOW_SECONDS).triggered,
          "re-borrow at exactly 24 hours is allowed");
    CHECK(!fraud_check_rapid_reborrow(-1, RAPID_REBORROW_WINDOW_SECONDS).triggered, "no prior return never triggers");

    /* 3. duplicate request id */
    CHECK(fraud_check_duplicate_request_id(1).triggered && !fraud_check_duplicate_request_id(0).triggered,
          "duplicate request ids are flagged");

    /* 4. condition manipulation */
    CHECK(!fraud_check_condition_manipulation(COND_NEW, COND_GOOD, 0, 1).triggered, "a one-level downgrade is allowed");
    CHECK(fraud_check_condition_manipulation(COND_NEW, COND_WORN, 0, 1).triggered, "a two-level downgrade is flagged");
    CHECK(!fraud_check_condition_manipulation(COND_NEW, COND_WORN, 1, 1).triggered,
          "a two-level downgrade with a librarian override is allowed");
    CHECK(!fraud_check_condition_manipulation(COND_WORN, COND_NEW, 0, 1).triggered, "an upgrade is never flagged");

    /* combining */
    FraudCheck checks[4] = { {0, ""}, {0, ""}, {0, ""}, {0, ""} };
    char reason[256];
    CHECK(fraud_evaluate(checks, reason) == 0, "no triggered heuristic: not suspicious");
    checks[0] = fraud_check_excessive_concurrent_borrows(4, 3);
    checks[2] = fraud_check_duplicate_request_id(1);
    CHECK(fraud_evaluate(checks, reason) == 1 && strstr(reason, "concurrent") && strstr(reason, "duplicate") && strstr(reason, "; "),
          "several triggers produce one combined reason");

    printf(failures == 0 ? "test_fraud: all checks passed\n" : "test_fraud: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
