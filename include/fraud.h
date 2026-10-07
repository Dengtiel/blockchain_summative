#ifndef FRAUD_H
#define FRAUD_H

#include "copy.h"

/* ============================================================
 * fraud.h
 * Four misuse heuristics over a borrow/return/condition-update
 * request. Each takes the already-gathered facts about that
 * request (counts, timestamps, flags) rather than the chain, pool,
 * or copy inventory directly, so the heuristics stay pure and easy
 * to test; the caller is responsible for gathering those facts.
 * A request that trips any heuristic is flagged SUSPICIOUS instead
 * of going straight to PENDING, and stays that way until a
 * librarian explicitly approves or rejects it.
 * ============================================================ */

#define DEFAULT_MAX_CONCURRENT_BORROWS  3
#define RAPID_REBORROW_WINDOW_SECONDS   (24 * 60 * 60)
#define MAX_CONDITION_DOWNGRADE_STEPS   1

typedef struct {
    int triggered;
    char reason[96];
} FraudCheck;

/* Heuristic 1: member already holds >= max_concurrent borrowed
 * copies before this new borrow. */
FraudCheck fraud_check_excessive_concurrent_borrows(int current_borrowed_count, int max_concurrent);

/* Heuristic 2: seconds_since_return < window_seconds means the
 * member is re-borrowing the same title too soon after returning
 * it. Pass a negative seconds_since_return when there's no prior
 * return on record (never triggers). */
FraudCheck fraud_check_rapid_reborrow(long seconds_since_return, long window_seconds);

/* Heuristic 3: request_id has already been seen, either still
 * pending in the pool or already sealed into a block. */
FraudCheck fraud_check_duplicate_request_id(int already_seen);

/* Heuristic 4: a copy's condition getting worse by more than
 * max_steps in one update, without an explicit librarian override. */
FraudCheck fraud_check_condition_manipulation(CopyCondition before, CopyCondition after,
                                               int librarian_override, int max_steps);

/* Runs the four results and returns 1 (suspicious) if any
 * triggered, writing a combined human-readable reason into
 * out_reason. Returns 0 if none triggered (out_reason untouched). */
int fraud_evaluate(const FraudCheck checks[4], char out_reason[256]);

#endif
