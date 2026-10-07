/* ============================================================
 * cmd_audit.c
 * fraud_review, approve_suspicious, reject_suspicious,
 * lending_history, member_history, book_history.
 * ============================================================ */

#include <stdio.h>
#include <string.h>
#include "cli.h"
#include "input.h"
#include "registry.h"

static void print_history_header(void) {
    printf("  %-5s %-12s %-9s %-9s %-12s %-11s %-5s %s\n", "BLOCK", "RECORD", "ACTION", "MEMBER", "COPY", "DUE", "FINE", "NONCE");
}

static void print_history_row(unsigned long block, const LendingRecord *r) {
    char due[12] = "-";
    if (r->due_date) cli_format_date(r->due_date, due);
    printf("  #%-4lu %-12s %-9s %-9s %-12s %-11s %-5d %lu\n", block, r->record_id, lending_action_to_string(r->action),
           r->member_id, r->copy_id[0] ? r->copy_id : "-", due, r->fine_amount, r->nonce);
}

static int cmd_fraud_review(CliContext *ctx, int argc, char **argv) {
    (void)argc; (void)argv;
    PendingPool *p = &ctx->state->pool;
    int n = 0;
    printf("Suspicious requests awaiting review (never mined until approved):\n");
    for (size_t i = 0; i < p->count; i++) {
        const PendingEntry *e = &p->entries[i];
        if (e->status != POOL_SUSPICIOUS) continue;
        char when[20];
        cli_format_time(e->submitted_at, when);
        printf("  %s  %s by %s  %s  submitted %s\n      reason: %s\n", e->request_id,
               e->kind == ENTRY_LENDING ? lending_action_to_string(e->record.action) : "TRANSFER",
               pending_entry_owner(e), e->kind == ENTRY_LENDING ? e->record.copy_id : "", when, e->fraud_reason);
        n++;
    }
    if (!n) printf("  (none)\n");
    else printf("%d flagged. Decide with: approve_suspicious <request_id>  or  reject_suspicious <request_id>\n", n);
    return 0;
}

static PendingEntry *find_suspicious(CliContext *ctx, const char *id) {
    PendingEntry *e = pending_pool_find(&ctx->state->pool, id);
    if (!e) { printf("Error: no request %s in the pending pool.\n", id); return NULL; }
    if (e->status != POOL_SUSPICIOUS) { printf("Error: request %s is not SUSPICIOUS (status %s).\n", id,
                                               e->status == POOL_CONFIRMED ? "CONFIRMED" : "PENDING"); return NULL; }
    return e;
}

static int cmd_approve_suspicious(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "approve_suspicious <request_id>")) return 1;
    PendingEntry *e = find_suspicious(ctx, argv[1]);
    if (!e) return 1;
    e->status = POOL_PENDING;
    printf("Librarian approved %s: it returns to PENDING and can now be mined (original flag: %s).\n", argv[1], e->fraud_reason);
    return 0;
}

static int cmd_reject_suspicious(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "reject_suspicious <request_id>")) return 1;
    PendingEntry *e = find_suspicious(ctx, argv[1]);
    if (!e) return 1;

    char owner[MEMBER_ID_LEN];
    int was_lending = e->kind == ENTRY_LENDING;
    snprintf(owner, sizeof(owner), "%s", pending_entry_owner(e));
    pending_pool_remove(&ctx->state->pool, argv[1]);
    if (was_lending) lending_renumber_pending(ctx->state, owner);
    printf("Librarian rejected %s: it was discarded from the pool%s.\n", argv[1],
           was_lending ? " and the member's remaining requests were re-numbered and re-signed" : "");
    return 0;
}

static int cmd_lending_history(CliContext *ctx, int argc, char **argv) {
    long limit = 50;
    if (argc >= 2 && input_parse_int(argv[1], 1, 100000, &limit) != 0) { printf("Error: the limit must be a positive whole number.\n"); return 1; }

    size_t total = 0;
    for (const Block *b = ctx->state->chain.head; b; b = b->next) total += b->record_count;
    printf("Lending history: %zu confirmed record(s) on the chain (showing the latest %ld).\n", total, limit);
    print_history_header();

    size_t skip = total > (size_t)limit ? total - (size_t)limit : 0, seen = 0;
    for (const Block *b = ctx->state->chain.head; b; b = b->next)
        for (size_t i = 0; i < b->record_count; i++)
            if (seen++ >= skip) print_history_row(b->index, &b->records[i]);
    if (!total) printf("  (no records yet)\n");
    return 0;
}

static int cmd_member_history(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "member_history <member_id>")) return 1;
    AppState *s = ctx->state;
    if (!registry_find_member(&s->registry, argv[1])) { printf("Error: unknown member id %s.\n", argv[1]); return 1; }

    printf("History of member %s:\n", argv[1]);
    print_history_header();
    int n = 0;
    for (const Block *b = s->chain.head; b; b = b->next)
        for (size_t i = 0; i < b->record_count; i++)
            if (strcmp(b->records[i].member_id, argv[1]) == 0) { print_history_row(b->index, &b->records[i]); n++; }
    if (!n) printf("  (no confirmed records)\n");
    for (size_t i = 0; i < s->pool.count; i++) {
        const PendingEntry *e = &s->pool.entries[i];
        if (e->kind == ENTRY_LENDING && strcmp(e->record.member_id, argv[1]) == 0)
            printf("  pool  %-12s %-9s %-9s %-12s (%s)\n", e->request_id, lending_action_to_string(e->record.action),
                   e->record.member_id, e->record.copy_id[0] ? e->record.copy_id : "-",
                   e->status == POOL_SUSPICIOUS ? "SUSPICIOUS" : "pending");
    }
    return 0;
}

static int cmd_book_history(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "book_history <book_id>")) return 1;
    AppState *s = ctx->state;
    if (!registry_find_book(&s->registry, argv[1])) { printf("Error: unknown book id %s.\n", argv[1]); return 1; }

    printf("History of book %s (all copies):\n", argv[1]);
    print_history_header();
    int n = 0;
    for (const Block *b = s->chain.head; b; b = b->next)
        for (size_t i = 0; i < b->record_count; i++)
            if (strcmp(b->records[i].book_id, argv[1]) == 0) { print_history_row(b->index, &b->records[i]); n++; }
    if (!n) printf("  (no confirmed records)\n");
    return 0;
}

const Command audit_commands[] = {
    { "fraud_review", cmd_fraud_review, "", "list SUSPICIOUS requests and why they were flagged", 0 },
    { "approve_suspicious", cmd_approve_suspicious, "<request_id>", "release a flagged request so it can be mined", 1 },
    { "reject_suspicious", cmd_reject_suspicious, "<request_id>", "discard a flagged request", 1 },
    { "lending_history", cmd_lending_history, "[limit]", "confirmed lending records on the chain", 0 },
    { "member_history", cmd_member_history, "<member_id>", "one member's confirmed and pending records", 0 },
    { "book_history", cmd_book_history, "<book_id>", "all records for one title", 0 },
    { NULL, NULL, NULL, NULL, 0 }
};
