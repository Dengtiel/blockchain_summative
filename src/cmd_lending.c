/* ============================================================
 * cmd_lending.c
 * borrow_book, return_book, renew_book, reserve_book,
 * reservation_status, check_overdue, view_fines, settle_fine.
 * Every one of these only queues a signed request; inventory and
 * balances change when a block is mined.
 * ============================================================ */

#include <stdio.h>
#include <string.h>
#include "cli.h"
#include "input.h"
#include "registry.h"

static void print_fraud_notice(const LendResult *r) {
    if (r->suspicious)
        printf("  FLAGGED SUSPICIOUS: %s\n  It will not be mined until a librarian runs approve_suspicious %s.\n",
               r->reason, r->request_id);
}

static int cmd_borrow_book(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 2, "borrow_book <book_id> <member_id> [due_date YYYY-MM-DD|-] [request_id]")) return 1;
    AppState *s = ctx->state;

    if (!input_valid_id(argv[1], BOOK_ID_LEN) || !input_valid_id(argv[2], MEMBER_ID_LEN)) {
        printf("Error: malformed book or member id.\n");
        return 1;
    }
    time_t due = 0;
    if (argc >= 4 && strcmp(argv[3], "-") != 0) {
        if (input_parse_date(argv[3], &due) != 0) { printf("Error: due date must be a real calendar date, YYYY-MM-DD.\n"); return 1; }
        if (!input_date_not_in_past(due, app_now(s))) { printf("Error: the due date is in the past.\n"); return 1; }
    }
    const char *forced_id = NULL;
    if (argc >= 5) {
        if (!input_valid_id(argv[4], REQUEST_ID_LEN)) { printf("Error: malformed request id.\n"); return 1; }
        forced_id = argv[4];
    }

    LendResult r;
    if (lending_borrow(s, argv[1], argv[2], due, forced_id, &r) != 0) {
        printf("Borrow rejected: %s\n", r.err);
        return 1;
    }
    char d[12];
    cli_format_date(r.due, d);
    printf("Borrow request %s signed by %s and added to the pending pool.\n  Copy %s, due %s, nonce %lu, status %s.\n",
           r.request_id, argv[2], r.copy_id, d, r.nonce, r.suspicious ? "SUSPICIOUS" : "PENDING");
    print_fraud_notice(&r);
    return 0;
}

static int parse_condition_arg(int argc, char **argv, int idx, int *has_cond, CopyCondition *cond) {
    *has_cond = 0;
    if (argc <= idx) return 0;
    if (copy_condition_from_string(argv[idx], cond) != 0 || *cond == COND_LOST) {
        printf("Error: condition must be NEW, GOOD or WORN (use mark_lost for a lost copy).\n");
        return -1;
    }
    *has_cond = 1;
    return 0;
}

static int cmd_return_book(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "return_book <copy_id> [NEW|GOOD|WORN] [override_reason]")) return 1;
    if (!input_valid_id(argv[1], COPY_ID_LEN)) { printf("Error: malformed copy id.\n"); return 1; }

    int has_cond;
    CopyCondition cond = COND_GOOD;
    if (parse_condition_arg(argc, argv, 2, &has_cond, &cond) != 0) return 1;
    const char *override = argc >= 4 ? argv[3] : NULL;

    LendResult r;
    if (lending_return(ctx->state, argv[1], has_cond, cond, override, &r) != 0) {
        printf("Return rejected: %s\n", r.err);
        return 1;
    }
    printf("Return request %s signed and added to the pending pool (copy %s, nonce %lu).\n", r.request_id, r.copy_id, r.nonce);
    if (r.late) printf("  LATE by %d day(s): fine %d token(s); reward on confirmation: %d.\n", r.days_late, r.fine, RETURN_BONUS_LATE);
    else printf("  On time: reward on confirmation: %d token(s).\n", RETURN_BONUS_ON_TIME);
    print_fraud_notice(&r);
    return 0;
}

static int cmd_renew_book(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "renew_book <copy_id>")) return 1;
    LendResult r;
    if (lending_renew(ctx->state, argv[1], &r) != 0) {
        printf("Renewal rejected: %s\n", r.err);
        return 1;
    }
    char d[12];
    cli_format_date(r.due, d);
    printf("Renewal request %s signed and added to the pending pool.\n  New due date %s, nonce %lu.\n", r.request_id, d, r.nonce);
    return 0;
}

static int cmd_reserve_book(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 2, "reserve_book <book_id> <member_id>")) return 1;
    LendResult r;
    if (lending_reserve(ctx->state, argv[1], argv[2], &r) != 0) {
        printf("Reservation rejected: %s\n", r.err);
        return 1;
    }
    printf("Reservation request %s signed and added to the pending pool (nonce %lu).\n"
           "  Once mined, %s joins the queue and is offered the next returned copy of %s.\n",
           r.request_id, r.nonce, argv[2], argv[1]);
    return 0;
}

static int cmd_reservation_status(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "reservation_status <book_id>")) return 1;
    AppState *s = ctx->state;
    if (!registry_find_book(&s->registry, argv[1])) { printf("Error: unknown book id %s.\n", argv[1]); return 1; }

    int pos = 0;
    printf("Reservation queue for %s (first in, first out):\n", argv[1]);
    for (size_t i = 0; i < s->reservations.count; i++) {
        const Reservation *r = &s->reservations.entries[i];
        if (strcmp(r->book_id, argv[1]) != 0) continue;
        char when[20];
        cli_format_time(r->reservation_date, when);
        printf("  %d. %-10s reserved %s\n", ++pos, r->member_id, when);
    }
    if (!pos) printf("  (empty)\n");

    for (size_t i = 0; i < s->copies.count; i++) {
        const Copy *c = &s->copies.copies[i];
        if (strcmp(c->book_id, argv[1]) == 0 && c->state == COPY_RESERVED)
            printf("  Copy %s is held at the desk for the member at the front of the queue.\n", c->copy_id);
    }
    int waiting_in_pool = 0;
    for (size_t i = 0; i < s->pool.count; i++) {
        const PendingEntry *e = &s->pool.entries[i];
        if (e->kind == ENTRY_LENDING && e->record.action == ACTION_RESERVED && strcmp(e->record.book_id, argv[1]) == 0) waiting_in_pool++;
    }
    if (waiting_in_pool) printf("  %d reservation request(s) are still in the pending pool.\n", waiting_in_pool);
    return 0;
}

static int cmd_check_overdue(CliContext *ctx, int argc, char **argv) {
    (void)argc; (void)argv;
    AppState *s = ctx->state;
    time_t now = app_now(s);
    int found = 0;
    char stamp[20];

    cli_format_time(now, stamp);
    printf("Overdue scan at %s:\n", stamp);
    for (size_t i = 0; i < s->copies.count; i++) {
        const Copy *c = &s->copies.copies[i];
        if (c->state != COPY_BORROWED || now <= c->due_date) continue;
        int days = lending_days_late(c->due_date, now);
        char d[12];
        cli_format_date(c->due_date, d);
        printf("  %-12s held by %-8s due %s  %d day(s) late  fine accrued %d\n", c->copy_id, c->current_holder_id, d,
               days, days * s->meta.fine_per_day);
        found++;
    }
    if (!found) printf("  No borrowed copy is past its due date.\n");

    int queued = lending_scan_overdue(s);
    printf("%d new OVERDUE flag(s) queued in the pending pool (signed by the librarian).\n", queued);
    return 0;
}

static void print_fine_line(AppState *s, const char *member_id) {
    FineSummary fs;
    lending_fine_summary(s, member_id, &fs);

    int accruing = 0;
    time_t now = app_now(s);
    for (size_t i = 0; i < s->copies.count; i++) {
        const Copy *c = &s->copies.copies[i];
        if (c->state == COPY_BORROWED && strcmp(c->current_holder_id, member_id) == 0 && now > c->due_date)
            accruing += lending_days_late(c->due_date, now) * s->meta.fine_per_day;
    }
    printf("  %-10s assessed %-4d settled %-4d pending %-4d outstanding %-4d accruing on overdue loans %d\n",
           member_id, fs.assessed, fs.settled, fs.pending, fs.outstanding, accruing);
}

static int cmd_view_fines(CliContext *ctx, int argc, char **argv) {
    AppState *s = ctx->state;
    printf("Fines (%d token(s) per day late):\n", s->meta.fine_per_day);
    if (argc >= 2) {
        if (!registry_find_member(&s->registry, argv[1])) { printf("Error: unknown member id %s.\n", argv[1]); return 1; }
        print_fine_line(s, argv[1]);
        return 0;
    }
    for (size_t i = 0; i < s->registry.member_count; i++) print_fine_line(s, s->registry.members[i].member_id);
    return 0;
}

static int cmd_settle_fine(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "settle_fine <member_id>")) return 1;
    LendResult r;
    if (lending_settle_fine(ctx->state, argv[1], &r) != 0) {
        printf("Settlement rejected: %s\n", r.err);
        return 1;
    }
    printf("Fine settlement %s queued: %d token(s) from %s to %s (overdue-related, so it is mined first).\n",
           r.request_id, r.amount, argv[1], LIBRARY_ACCOUNT);
    return 0;
}

const Command lending_commands[] = {
    { "borrow_book", cmd_borrow_book, "<book_id> <member_id> [due_date|-] [request_id]", "queue a signed borrow request", 1 },
    { "return_book", cmd_return_book, "<copy_id> [NEW|GOOD|WORN] [override_reason]", "queue a signed return (late fines computed)", 1 },
    { "renew_book", cmd_renew_book, "<copy_id>", "queue a renewal extending the due date", 1 },
    { "reserve_book", cmd_reserve_book, "<book_id> <member_id>", "join the reservation queue for a title", 1 },
    { "reservation_status", cmd_reservation_status, "<book_id>", "show a title's reservation queue", 0 },
    { "check_overdue", cmd_check_overdue, "", "scan loans, show fines accruing and queue OVERDUE flags", 1 },
    { "view_fines", cmd_view_fines, "[member_id]", "assessed, settled and outstanding fines", 0 },
    { "settle_fine", cmd_settle_fine, "<member_id>", "pay a member's outstanding fine in tokens", 1 },
    { NULL, NULL, NULL, NULL, 0 }
};
