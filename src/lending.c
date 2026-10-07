/* ============================================================
 * lending.c
 * Builds, screens, signs and queues lending and token requests.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lending.h"
#include "fraud.h"
#include "input.h"
#include "mining.h"

#define SECONDS_PER_DAY (24L * 60 * 60)

static unsigned long g_req_counter = 0;
static unsigned long g_txn_counter = 0;

/* ---- id counters ---- */

static void next_request_id(char out[REQUEST_ID_LEN]) {
    snprintf(out, REQUEST_ID_LEN, "REQ%06lu", ++g_req_counter);
}

static void next_txn_id(char out[REQUEST_ID_LEN]) {
    snprintf(out, REQUEST_ID_LEN, "TXN%06lu", ++g_txn_counter);
}

static unsigned long numeric_suffix(const char *id, const char *prefix) {
    size_t n = strlen(prefix);
    if (strncmp(id, prefix, n) != 0) return 0;
    return strtoul(id + n, NULL, 10);
}

void lending_init(const AppState *state) {
    unsigned long max_req = 0, max_txn = state->utxo_ledger.tx_counter;
    if (state->account_ledger.tx_counter > max_txn) max_txn = state->account_ledger.tx_counter;

    for (const Block *b = state->chain.head; b != NULL; b = b->next) {
        for (size_t i = 0; i < b->record_count; i++) {
            unsigned long n = numeric_suffix(b->records[i].record_id, "REQ");
            if (n > max_req) max_req = n;
        }
    }
    for (size_t i = 0; i < state->pool.count; i++) {
        const char *id = state->pool.entries[i].request_id;
        unsigned long n = numeric_suffix(id, "REQ");
        if (n > max_req) max_req = n;
        n = numeric_suffix(id, "TXN");
        if (n > max_txn) max_txn = n;
    }
    g_req_counter = max_req;
    g_txn_counter = max_txn;
}

/* ---- chain queries ---- */

unsigned long lending_chain_nonce(const AppState *state, const char *member_id) {
    unsigned long n = 0;
    for (const Block *b = state->chain.head; b != NULL; b = b->next) {
        for (size_t i = 0; i < b->record_count; i++) {
            if (strcmp(b->records[i].member_id, member_id) == 0) n++;
        }
    }
    return n;
}

int lending_chain_has_request_id(const Chain *chain, const char *request_id) {
    for (const Block *b = chain->head; b != NULL; b = b->next) {
        for (size_t i = 0; i < b->record_count; i++) {
            if (strcmp(b->records[i].record_id, request_id) == 0) return 1;
        }
    }
    return 0;
}

const LendingRecord *lending_last_borrow(const Chain *chain, const char *copy_id) {
    const LendingRecord *last = NULL;
    for (const Block *b = chain->head; b != NULL; b = b->next) {
        for (size_t i = 0; i < b->record_count; i++) {
            const LendingRecord *r = &b->records[i];
            if (r->action == ACTION_BORROWED && strcmp(r->copy_id, copy_id) == 0) last = r;
        }
    }
    return last;
}

/* Seconds since member_id last returned a copy of book_id (block time of
 * that RETURNED record), or -1 if they never have. */
static long seconds_since_last_return(const AppState *state, const char *member_id,
                                      const char *book_id, time_t now) {
    time_t latest = 0;
    int found = 0;
    for (const Block *b = state->chain.head; b != NULL; b = b->next) {
        for (size_t i = 0; i < b->record_count; i++) {
            const LendingRecord *r = &b->records[i];
            if (r->action == ACTION_RETURNED && strcmp(r->member_id, member_id) == 0 &&
                strcmp(r->book_id, book_id) == 0 && (!found || b->timestamp > latest)) {
                latest = b->timestamp;
                found = 1;
            }
        }
    }
    return found ? (long)(now - latest) : -1;
}

/* Is there already an OVERDUE flag for this loan (same copy, same due date)? */
static int overdue_already_flagged(const AppState *state, const Copy *c) {
    for (const Block *b = state->chain.head; b != NULL; b = b->next) {
        for (size_t i = 0; i < b->record_count; i++) {
            const LendingRecord *r = &b->records[i];
            if (r->action == ACTION_OVERDUE && strcmp(r->copy_id, c->copy_id) == 0 &&
                r->due_date == c->due_date) return 1;
        }
    }
    for (size_t i = 0; i < state->pool.count; i++) {
        const PendingEntry *e = &state->pool.entries[i];
        if (e->kind == ENTRY_LENDING && e->record.action == ACTION_OVERDUE &&
            strcmp(e->record.copy_id, c->copy_id) == 0 && e->record.due_date == c->due_date) return 1;
    }
    return 0;
}

int lending_days_late(time_t due, time_t now) {
    if (now <= due) return 0;
    return (int)((now - due + SECONDS_PER_DAY - 1) / SECONDS_PER_DAY);   /* rounded up */
}

/* ---- pool queries ---- */

static int pending_action_count(const AppState *state, const char *member_id, LendingAction action) {
    int n = 0;
    for (size_t i = 0; i < state->pool.count; i++) {
        const PendingEntry *e = &state->pool.entries[i];
        if (e->kind == ENTRY_LENDING && e->status != POOL_CONFIRMED && e->record.action == action &&
            strcmp(e->record.member_id, member_id) == 0) n++;
    }
    return n;
}

static int copy_has_pending_action(const AppState *state, const char *copy_id, LendingAction action) {
    int n = 0;
    for (size_t i = 0; i < state->pool.count; i++) {
        const PendingEntry *e = &state->pool.entries[i];
        if (e->kind == ENTRY_LENDING && e->status != POOL_CONFIRMED && e->record.action == action &&
            strcmp(e->record.copy_id, copy_id) == 0) n++;
    }
    return n;
}

/* Does a still-unmined request already claim this copy for a borrow? */
static int copy_claimed_by_pending_borrow(const AppState *state, const Copy *c) {
    return copy_has_pending_action(state, c->copy_id, ACTION_BORROWED) > 0;
}

/* Inventory only changes when a request is mined, so two pending borrows
 * of the same title must not be matched to the same copy. */
static Copy *find_free_copy(AppState *state, const char *book_id, CopyState wanted) {
    for (size_t i = 0; i < state->copies.count; i++) {
        Copy *c = &state->copies.copies[i];
        if (strcmp(c->book_id, book_id) != 0 || c->state != wanted) continue;
        if (!copy_claimed_by_pending_borrow(state, c)) return c;
    }
    return NULL;
}

/* ---- building and queueing a record ---- */

static PendingEntry *queue_record(AppState *state, const char *request_id, const Book *book,
                                  const Member *member, const char *copy_id, LendingAction action,
                                  time_t due, int fine, int overdue_related, time_t reservation_date,
                                  const char *signer, char *err, size_t err_size) {
    LendingRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.record_id, RECORD_ID_LEN, "%s", request_id);
    snprintf(rec.book_id, BOOK_ID_LEN, "%s", book->book_id);
    snprintf(rec.copy_id, COPY_ID_LEN, "%s", copy_id ? copy_id : "");
    snprintf(rec.book_title, TITLE_LEN, "%s", book->title);
    snprintf(rec.member_id, MEMBER_ID_LEN, "%s", member->member_id);
    snprintf(rec.member_name, NAME_LEN, "%s", member->full_name);
    rec.action = action;
    rec.due_date = due;
    rec.fine_amount = fine;
    rec.nonce = lending_chain_nonce(state, member->member_id) +
                (unsigned long)pending_pool_count_lending(&state->pool, member->member_id);

    if (record_sign(&rec, signer) != 0) {
        snprintf(err, err_size, "Could not sign the request (key error for %s).", signer);
        return NULL;
    }

    PendingEntry *e = pending_pool_add_lending(&state->pool, request_id, &rec, overdue_related,
                                               reservation_date, app_now(state));
    if (!e) snprintf(err, err_size, "The pending pool is full or already holds request %s.", request_id);
    return e;
}

static const Book *lookup_book(AppState *state, const char *book_id, char *err, size_t n) {
    Book *b = registry_find_book(&state->registry, book_id);
    if (!b) snprintf(err, n, "Unknown book id %s.", book_id);
    return b;
}

static const Member *lookup_member(AppState *state, const char *member_id, char *err, size_t n) {
    Member *m = registry_find_member(&state->registry, member_id);
    if (!m) snprintf(err, n, "Unknown member id %s.", member_id);
    return m;
}

/* ---- borrow ---- */

int lending_borrow(AppState *state, const char *book_id, const char *member_id,
                   time_t due_override, const char *forced_request_id, LendResult *out) {
    memset(out, 0, sizeof(*out));
    const Book *book = lookup_book(state, book_id, out->err, sizeof(out->err));
    if (!book) return -1;
    const Member *member = lookup_member(state, member_id, out->err, sizeof(out->err));
    if (!member) return -1;

    /* A copy offered to this member from the reservation queue comes first. */
    Copy *target = NULL;
    time_t reservation_date = 0;
    Reservation *front = reservation_peek_front(&state->reservations, book_id);
    if (front && strcmp(front->member_id, member_id) == 0) {
        target = find_free_copy(state, book_id, COPY_RESERVED);
        if (target) reservation_date = front->reservation_date;
    }
    if (!target) target = find_free_copy(state, book_id, COPY_AVAILABLE);

    if (!target) {
        if (front && strcmp(front->member_id, member_id) != 0 && copy_count_for_book(&state->copies, book_id) > 0) {
            snprintf(out->err, sizeof(out->err),
                     "No copy of %s is free for you: the next returned copy is reserved for another member.", book_id);
        } else {
            snprintf(out->err, sizeof(out->err),
                     "No copy of %s is available. Use: reserve_book %s %s", book_id, book_id, member_id);
        }
        return -1;
    }

    char request_id[REQUEST_ID_LEN];
    int duplicate = 0;
    if (forced_request_id) {
        snprintf(request_id, sizeof(request_id), "%s", forced_request_id);
        duplicate = pending_pool_has_request_id(&state->pool, request_id) ||
                    lending_chain_has_request_id(&state->chain, request_id);
        if (duplicate) {
            /* Keep the replayed request in the pool under a derived id so it
             * can be reviewed, but never let it share its original's id. */
            char base[REQUEST_ID_LEN];
            snprintf(base, sizeof(base), "%.20s", forced_request_id); /* leaves room for -DUPnnn */
            for (int k = 2; k < 1000; k++) {
                snprintf(request_id, sizeof(request_id), "%.20s-DUP%d", base, k);
                if (!pending_pool_has_request_id(&state->pool, request_id) &&
                    !lending_chain_has_request_id(&state->chain, request_id)) break;
            }
        }
    } else {
        next_request_id(request_id);
    }

    time_t now = app_now(state);
    int concurrent = copy_count_borrowed_by(&state->copies, member_id) +
                     pending_action_count(state, member_id, ACTION_BORROWED);

    FraudCheck checks[4];
    checks[0] = fraud_check_excessive_concurrent_borrows(concurrent, state->meta.max_concurrent);
    checks[1] = fraud_check_rapid_reborrow(seconds_since_last_return(state, member_id, book_id, now),
                                           RAPID_REBORROW_WINDOW_SECONDS);
    checks[2] = fraud_check_duplicate_request_id(duplicate);
    checks[3] = (FraudCheck){ 0, "" };

    char reason[256];
    int suspicious = fraud_evaluate(checks, reason);

    time_t due = due_override ? due_override : now + (time_t)state->meta.loan_days * SECONDS_PER_DAY;

    PendingEntry *e = queue_record(state, request_id, book, member, target->copy_id, ACTION_BORROWED,
                                   due, 0, 0, reservation_date, member_id, out->err, sizeof(out->err));
    if (!e) return -1;

    if (suspicious) {
        pending_pool_mark_suspicious(&state->pool, request_id, reason);
        out->suspicious = 1;
        snprintf(out->reason, sizeof(out->reason), "%.159s", reason);
    }

    snprintf(out->request_id, sizeof(out->request_id), "%s", request_id);
    snprintf(out->copy_id, sizeof(out->copy_id), "%s", target->copy_id);
    out->due = due;
    out->nonce = e->record.nonce;
    return 0;
}

/* ---- return ---- */

int lending_return(AppState *state, const char *copy_id, int has_condition, CopyCondition condition,
                   const char *override_reason, LendResult *out) {
    memset(out, 0, sizeof(*out));
    Copy *c = copy_find(&state->copies, copy_id);
    if (!c) { snprintf(out->err, sizeof(out->err), "Unknown copy id %s.", copy_id); return -1; }
    if (c->state != COPY_BORROWED) {
        snprintf(out->err, sizeof(out->err), "Copy %s is not currently borrowed.", copy_id);
        return -1;
    }
    if (has_condition && condition == COND_LOST) {
        snprintf(out->err, sizeof(out->err), "Use mark_lost to report a lost copy, not a return.");
        return -1;
    }

    /* A return must correspond to a BORROWED record already confirmed on the chain. */
    const LendingRecord *borrow = lending_last_borrow(&state->chain, copy_id);
    if (!borrow) {
        snprintf(out->err, sizeof(out->err),
                 "No confirmed BORROWED record exists on the chain for copy %s (mine the borrow first).", copy_id);
        return -1;
    }
    if (copy_has_pending_action(state, copy_id, ACTION_RETURNED)) {
        snprintf(out->err, sizeof(out->err), "A return of %s is already waiting in the pool.", copy_id);
        return -1;
    }

    const Member *member = lookup_member(state, c->current_holder_id, out->err, sizeof(out->err));
    if (!member) return -1;
    const Book *book = lookup_book(state, c->book_id, out->err, sizeof(out->err));
    if (!book) return -1;

    time_t now = app_now(state);
    int days_late = lending_days_late(c->due_date, now);
    int fine = days_late * state->meta.fine_per_day;

    FraudCheck checks[4] = { {0, ""}, {0, ""}, {0, ""}, {0, ""} };
    if (has_condition) {
        checks[3] = fraud_check_condition_manipulation(c->condition, condition,
                                                       override_reason != NULL && override_reason[0] != '\0',
                                                       MAX_CONDITION_DOWNGRADE_STEPS);
    }
    char reason[256];
    int suspicious = fraud_evaluate(checks, reason);

    char request_id[REQUEST_ID_LEN];
    next_request_id(request_id);

    PendingEntry *e = queue_record(state, request_id, book, member, copy_id, ACTION_RETURNED,
                                   c->due_date, fine, days_late > 0, 0, member->member_id,
                                   out->err, sizeof(out->err));
    if (!e) return -1;

    if (has_condition) {
        e->has_condition = 1;
        e->new_condition = condition;
    }
    if (suspicious) {
        pending_pool_mark_suspicious(&state->pool, request_id, reason);
        out->suspicious = 1;
        snprintf(out->reason, sizeof(out->reason), "%.159s", reason);
    }

    snprintf(out->request_id, sizeof(out->request_id), "%s", request_id);
    snprintf(out->copy_id, sizeof(out->copy_id), "%s", copy_id);
    out->late = days_late > 0;
    out->days_late = days_late;
    out->fine = fine;
    out->due = c->due_date;
    out->nonce = e->record.nonce;
    return 0;
}

/* ---- renew ---- */

int lending_renew(AppState *state, const char *copy_id, LendResult *out) {
    memset(out, 0, sizeof(*out));
    Copy *c = copy_find(&state->copies, copy_id);
    if (!c) { snprintf(out->err, sizeof(out->err), "Unknown copy id %s.", copy_id); return -1; }
    if (c->state != COPY_BORROWED) {
        snprintf(out->err, sizeof(out->err), "Copy %s is not currently borrowed.", copy_id);
        return -1;
    }

    time_t now = app_now(state);
    if (now > c->due_date) {
        snprintf(out->err, sizeof(out->err), "Copy %s is already overdue: return it instead of renewing.", copy_id);
        return -1;
    }
    if (reservation_has_other_than(&state->reservations, c->book_id, c->current_holder_id)) {
        snprintf(out->err, sizeof(out->err),
                 "Cannot renew: another member has a pending reservation on %s.", c->book_id);
        return -1;
    }
    if (c->renewal_count + copy_has_pending_action(state, copy_id, ACTION_RENEWED) >= state->meta.max_renewals) {
        snprintf(out->err, sizeof(out->err), "Copy %s has already been renewed the maximum of %d time(s).",
                 copy_id, state->meta.max_renewals);
        return -1;
    }

    const Member *member = lookup_member(state, c->current_holder_id, out->err, sizeof(out->err));
    if (!member) return -1;
    const Book *book = lookup_book(state, c->book_id, out->err, sizeof(out->err));
    if (!book) return -1;

    time_t new_due = c->due_date + (time_t)state->meta.renewal_days * SECONDS_PER_DAY;
    char request_id[REQUEST_ID_LEN];
    next_request_id(request_id);

    PendingEntry *e = queue_record(state, request_id, book, member, copy_id, ACTION_RENEWED, new_due, 0, 0, 0,
                                   member->member_id, out->err, sizeof(out->err));
    if (!e) return -1;

    snprintf(out->request_id, sizeof(out->request_id), "%s", request_id);
    snprintf(out->copy_id, sizeof(out->copy_id), "%s", copy_id);
    out->due = new_due;
    out->nonce = e->record.nonce;
    return 0;
}

/* ---- reserve ---- */

int lending_reserve(AppState *state, const char *book_id, const char *member_id, LendResult *out) {
    memset(out, 0, sizeof(*out));
    const Book *book = lookup_book(state, book_id, out->err, sizeof(out->err));
    if (!book) return -1;
    const Member *member = lookup_member(state, member_id, out->err, sizeof(out->err));
    if (!member) return -1;

    if (find_free_copy(state, book_id, COPY_AVAILABLE)) {
        snprintf(out->err, sizeof(out->err), "A copy of %s is available right now: use borrow_book instead.", book_id);
        return -1;
    }

    int usable = 0;
    for (size_t i = 0; i < state->copies.count; i++) {
        const Copy *c = &state->copies.copies[i];
        if (strcmp(c->book_id, book_id) == 0 && c->state != COPY_LOST) usable++;
    }
    if (usable == 0) {
        snprintf(out->err, sizeof(out->err), "Every copy of %s is lost or none are registered: nothing to reserve.", book_id);
        return -1;
    }

    if (reservation_has_member(&state->reservations, book_id, member_id)) {
        snprintf(out->err, sizeof(out->err), "%s already has a reservation on %s.", member_id, book_id);
        return -1;
    }
    for (size_t i = 0; i < state->pool.count; i++) {
        const PendingEntry *e = &state->pool.entries[i];
        if (e->kind == ENTRY_LENDING && e->status != POOL_CONFIRMED && e->record.action == ACTION_RESERVED &&
            strcmp(e->record.book_id, book_id) == 0 && strcmp(e->record.member_id, member_id) == 0) {
            snprintf(out->err, sizeof(out->err), "A reservation of %s by %s is already waiting in the pool.", book_id, member_id);
            return -1;
        }
    }
    for (size_t i = 0; i < state->copies.count; i++) {
        const Copy *c = &state->copies.copies[i];
        if (strcmp(c->book_id, book_id) == 0 && c->state == COPY_BORROWED &&
            strcmp(c->current_holder_id, member_id) == 0) {
            snprintf(out->err, sizeof(out->err), "%s already holds a copy of %s.", member_id, book_id);
            return -1;
        }
    }

    char request_id[REQUEST_ID_LEN];
    next_request_id(request_id);
    time_t now = app_now(state);

    PendingEntry *e = queue_record(state, request_id, book, member, "", ACTION_RESERVED, 0, 0, 0, now,
                                   member_id, out->err, sizeof(out->err));
    if (!e) return -1;

    snprintf(out->request_id, sizeof(out->request_id), "%s", request_id);
    out->nonce = e->record.nonce;
    return 0;
}

/* ---- overdue detection ---- */

int lending_scan_overdue(AppState *state) {
    time_t now = app_now(state);
    int queued = 0;

    for (size_t i = 0; i < state->copies.count; i++) {
        const Copy *c = &state->copies.copies[i];
        if (c->state != COPY_BORROWED || now <= c->due_date) continue;
        if (overdue_already_flagged(state, c)) continue;

        const Book *book = registry_find_book(&state->registry, c->book_id);
        const Member *member = registry_find_member(&state->registry, c->current_holder_id);
        if (!book || !member) continue;

        char request_id[REQUEST_ID_LEN], err[200];
        next_request_id(request_id);
        int fine = lending_days_late(c->due_date, now) * state->meta.fine_per_day;

        if (queue_record(state, request_id, book, member, c->copy_id, ACTION_OVERDUE, c->due_date, fine, 1, 0,
                         LIBRARIAN_ID, err, sizeof(err))) queued++;
    }
    return queued;
}

/* ---- token transfers ---- */

static int pending_outgoing_total(const AppState *state, const char *sender, int mode) {
    int total = 0;
    for (size_t i = 0; i < state->pool.count; i++) {
        const PendingEntry *e = &state->pool.entries[i];
        if (e->kind == ENTRY_TRANSFER && e->status != POOL_CONFIRMED && e->tx.ledger_mode == mode &&
            strcmp(e->tx.sender, sender) == 0) total += e->tx.amount + e->tx.fee;
    }
    return total;
}

static int ledger_balance(AppState *state, const char *id) {
    return state->meta.ledger_mode == LEDGER_MODE_UTXO ? utxo_balance(&state->utxo_ledger, id)
                                                      : account_balance(&state->account_ledger, id);
}

int lending_submit_transfer(AppState *state, const char *sender, const char *recipient,
                            int amount, int fee, int has_nonce, unsigned long explicit_nonce,
                            int is_fine, LendResult *out) {
    memset(out, 0, sizeof(*out));
    int mode = (int)state->meta.ledger_mode;

    if (amount <= 0) { snprintf(out->err, sizeof(out->err), "The amount must be a positive number of tokens."); return -1; }
    if (fee < 0) { snprintf(out->err, sizeof(out->err), "The fee cannot be negative."); return -1; }
    if (strcmp(sender, recipient) == 0) { snprintf(out->err, sizeof(out->err), "Sender and recipient must differ."); return -1; }

    int available = ledger_balance(state, sender) - pending_outgoing_total(state, sender, mode);
    if (available < amount + fee) {
        snprintf(out->err, sizeof(out->err),
                 "Insufficient balance: %s has %d token(s) available but needs %d (amount %d + fee %d).",
                 sender, available, amount + fee, amount, fee);
        return -1;
    }

    unsigned long expected_nonce = 0;
    if (mode == LEDGER_MODE_ACCOUNT) {
        expected_nonce = account_next_nonce(&state->account_ledger, sender) +
                         (unsigned long)pending_pool_count_transfers(&state->pool, sender);
        if (has_nonce && explicit_nonce != expected_nonce) {
            snprintf(out->err, sizeof(out->err),
                     "Incorrect or reused nonce %lu: %s's next expected nonce is %lu.",
                     explicit_nonce, sender, expected_nonce);
            return -1;
        }
    }

    TokenTx tx;
    memset(&tx, 0, sizeof(tx));
    snprintf(tx.sender, MEMBER_ID_LEN, "%s", sender);
    snprintf(tx.recipient, MEMBER_ID_LEN, "%s", recipient);
    tx.amount = amount;
    tx.fee = fee;
    tx.nonce = expected_nonce;
    tx.ledger_mode = mode;
    tx.is_fine = is_fine;

    char request_id[REQUEST_ID_LEN];
    next_txn_id(request_id);
    PendingEntry *e = pending_pool_add_transfer(&state->pool, request_id, &tx, is_fine, app_now(state));
    if (!e) { snprintf(out->err, sizeof(out->err), "The pending pool is full."); return -1; }

    snprintf(out->request_id, sizeof(out->request_id), "%s", request_id);
    out->amount = amount;
    out->nonce = expected_nonce;
    return 0;
}

/* ---- fines ---- */

void lending_fine_summary(const AppState *state, const char *member_id, FineSummary *out) {
    memset(out, 0, sizeof(*out));
    for (const Block *b = state->chain.head; b != NULL; b = b->next) {
        for (size_t i = 0; i < b->record_count; i++) {
            const LendingRecord *r = &b->records[i];
            if (r->action == ACTION_RETURNED && strcmp(r->member_id, member_id) == 0) out->assessed += r->fine_amount;
        }
    }
    out->settled = fines_settled(&state->fines, member_id);
    for (size_t i = 0; i < state->pool.count; i++) {
        const PendingEntry *e = &state->pool.entries[i];
        if (e->kind == ENTRY_TRANSFER && e->tx.is_fine && e->status != POOL_CONFIRMED &&
            strcmp(e->tx.sender, member_id) == 0) out->pending += e->tx.amount;
    }
    out->outstanding = out->assessed - out->settled - out->pending;
    if (out->outstanding < 0) out->outstanding = 0;
}

int lending_settle_fine(AppState *state, const char *member_id, LendResult *out) {
    memset(out, 0, sizeof(*out));
    if (!registry_find_member(&state->registry, member_id)) {
        snprintf(out->err, sizeof(out->err), "Unknown member id %s.", member_id);
        return -1;
    }

    FineSummary fs;
    lending_fine_summary(state, member_id, &fs);
    if (fs.outstanding <= 0) {
        snprintf(out->err, sizeof(out->err), "%s has no outstanding fine to settle.", member_id);
        return -1;
    }
    return lending_submit_transfer(state, member_id, LIBRARY_ACCOUNT, fs.outstanding, 0, 0, 0, 1, out);
}

/* ---- keeping nonces consecutive ---- */

void lending_renumber_pending(AppState *state, const char *member_id) {
    unsigned long next = lending_chain_nonce(state, member_id);

    /* hand out the lowest remaining old nonce first, so relative order holds */
    for (;;) {
        PendingEntry *pick = NULL;
        for (size_t i = 0; i < state->pool.count; i++) {
            PendingEntry *e = &state->pool.entries[i];
            if (e->kind != ENTRY_LENDING || e->status == POOL_CONFIRMED) continue;
            if (strcmp(e->record.member_id, member_id) != 0 || e->record.nonce < next) continue;
            if (!pick || e->record.nonce < pick->record.nonce) pick = e;
        }
        if (!pick) break;

        if (pick->record.nonce != next) {
            pick->record.nonce = next;
            record_sign(&pick->record, pick->record.signer_id);
        }
        next++;
    }
}
