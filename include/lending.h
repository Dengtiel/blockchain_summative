#ifndef LENDING_H
#define LENDING_H

#include "persistence.h"

/* ============================================================
 * lending.h
 * The lending service: turns a user's intent (borrow, return,
 * renew, reserve, settle a fine, transfer tokens) into a signed
 * request in the pending pool. Nothing here changes inventory or
 * balances -- that happens only when a miner confirms the request
 * (see confirm.h).
 *
 * Nonces: a member's chain nonce is the number of their records
 * already on the chain. A new request carries that number plus the
 * number of their requests still waiting in the pool, so several
 * requests queue with consecutive nonces and are confirmed in order.
 * ============================================================ */

typedef struct {
    char request_id[REQUEST_ID_LEN];
    char copy_id[COPY_ID_LEN];
    int suspicious;                 /* flagged by a fraud heuristic */
    char reason[FRAUD_REASON_LEN];  /* why it was flagged */
    int late;                       /* returns: past the due date */
    int fine;                       /* returns / overdue: fine in tokens */
    int days_late;
    time_t due;                     /* borrow / renew: the due date recorded */
    unsigned long nonce;
    int amount;                     /* transfers / fine settlement */
    char err[200];                  /* filled when a call returns -1 */
} LendResult;

/* Re-derives the request counters from whatever is already on the chain
 * and in the pool, so a restart never reuses an id. Call after loading. */
void lending_init(const AppState *state);

/* Number of records member_id already has on the chain: the nonce the
 * next confirmed record must carry. */
unsigned long lending_chain_nonce(const AppState *state, const char *member_id);

/* Each returns 0 and fills `out` on success, or -1 with out->err set. */
int lending_borrow(AppState *state, const char *book_id, const char *member_id,
                   time_t due_override, const char *forced_request_id, LendResult *out);
int lending_return(AppState *state, const char *copy_id, int has_condition, CopyCondition condition,
                   const char *override_reason, LendResult *out);
int lending_renew(AppState *state, const char *copy_id, LendResult *out);
int lending_reserve(AppState *state, const char *book_id, const char *member_id, LendResult *out);

/* Queues an OVERDUE record (signed by the librarian) for every borrowed
 * copy that is past due and not yet flagged. Returns how many were queued. */
int lending_scan_overdue(AppState *state);

/* Token transfers go through the pool too. `explicit_nonce` is used only
 * in account mode (pass has_nonce = 0 to auto-fill the expected nonce). */
int lending_submit_transfer(AppState *state, const char *sender, const char *recipient,
                            int amount, int fee, int has_nonce, unsigned long explicit_nonce,
                            int is_fine, LendResult *out);

/* Fines: assessed comes from the chain, settled from the fine book,
 * pending from not-yet-mined settlement transfers. */
typedef struct {
    int assessed;
    int settled;
    int pending;
    int outstanding;    /* assessed - settled - pending */
} FineSummary;

void lending_fine_summary(const AppState *state, const char *member_id, FineSummary *out);
int lending_settle_fine(AppState *state, const char *member_id, LendResult *out);

/* Days late (rounded up, minimum 1 once past due) and the resulting fine. */
int lending_days_late(time_t due, time_t now);

/* The latest BORROWED record for copy_id on the chain, or NULL. */
const LendingRecord *lending_last_borrow(const Chain *chain, const char *copy_id);

/* After an entry leaves the pool without being mined (rejected or
 * discarded), re-numbers and re-signs that member's remaining requests
 * so their nonces stay consecutive. */
void lending_renumber_pending(AppState *state, const char *member_id);

/* True if request_id is already on the chain. */
int lending_chain_has_request_id(const Chain *chain, const char *request_id);

#endif
