/* ============================================================
 * fraud.c
 * Four misuse/fraud heuristics, kept pure (no chain/pool/copy
 * dependency) so each is independently testable.
 * ============================================================ */

#include <stdio.h>
#include <string.h>
#include "fraud.h"

FraudCheck fraud_check_excessive_concurrent_borrows(int current_borrowed_count, int max_concurrent) {
    FraudCheck r = { 0, "" };
    if (current_borrowed_count >= max_concurrent) {
        r.triggered = 1;
        snprintf(r.reason, sizeof(r.reason),
                 "member already holds %d concurrent borrows (limit %d)",
                 current_borrowed_count, max_concurrent);
    }
    return r;
}

FraudCheck fraud_check_rapid_reborrow(long seconds_since_return, long window_seconds) {
    FraudCheck r = { 0, "" };
    if (seconds_since_return >= 0 && seconds_since_return < window_seconds) {
        r.triggered = 1;
        snprintf(r.reason, sizeof(r.reason),
                 "re-borrowed %ld seconds after return (window %ld)",
                 seconds_since_return, window_seconds);
    }
    return r;
}

FraudCheck fraud_check_duplicate_request_id(int already_seen) {
    FraudCheck r = { 0, "" };
    if (already_seen) {
        r.triggered = 1;
        snprintf(r.reason, sizeof(r.reason), "duplicate request_id already seen");
    }
    return r;
}

FraudCheck fraud_check_condition_manipulation(CopyCondition before, CopyCondition after,
                                               int librarian_override, int max_steps) {
    FraudCheck r = { 0, "" };
    int steps = (int)after - (int)before;
    if (steps > max_steps && !librarian_override) {
        r.triggered = 1;
        snprintf(r.reason, sizeof(r.reason),
                 "condition downgraded %d steps without librarian override (max %d)",
                 steps, max_steps);
    }
    return r;
}

int fraud_evaluate(const FraudCheck checks[4], char out_reason[256]) {
    int any = 0;
    out_reason[0] = '\0';

    for (int i = 0; i < 4; i++) {
        if (!checks[i].triggered) continue;
        any = 1;
        if (out_reason[0] != '\0') {
            strncat(out_reason, "; ", 256 - strlen(out_reason) - 1);
        }
        strncat(out_reason, checks[i].reason, 256 - strlen(out_reason) - 1);
    }

    return any;
}
