/* ============================================================
 * cmd_book.c
 * register_book, view_book, add_copy, view_copy, mark_lost.
 * ============================================================ */

#include <stdio.h>
#include <string.h>
#include "cli.h"
#include "input.h"
#include "registry.h"

static int cmd_register_book(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 3, "register_book <book_id> \"<title>\" \"<author>\"")) return 1;
    AppState *s = ctx->state;
    if (!input_valid_id(argv[1], BOOK_ID_LEN)) { printf("Error: invalid book id '%s'.\n", argv[1]); return 1; }
    if (!input_valid_text(argv[2], TITLE_LEN)) { printf("Error: invalid title.\n"); return 1; }
    if (!input_valid_text(argv[3], AUTHOR_LEN)) { printf("Error: invalid author.\n"); return 1; }
    if (registry_add_book(&s->registry, argv[1], argv[2], argv[3]) != 0) {
        printf("Error: book %s already exists or the registry is full.\n", argv[1]);
        return 1;
    }
    printf("Registered book %s: \"%s\" by %s. Add physical copies with: add_copy %s\n", argv[1], argv[2], argv[3], argv[1]);
    return 0;
}

static int cmd_view_book(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "view_book <book_id>")) return 1;
    AppState *s = ctx->state;
    Book *b = registry_find_book(&s->registry, argv[1]);
    if (!b) { printf("Error: unknown book id %s.\n", argv[1]); return 1; }

    int total = copy_count_for_book(&s->copies, b->book_id);
    printf("Book %s: \"%s\" by %s\n  Copies: %d total, %d available\n", b->book_id, b->title, b->author,
           total, copy_count_available(&s->copies, b->book_id));
    for (size_t i = 0; i < s->copies.count; i++) {
        const Copy *c = &s->copies.copies[i];
        if (strcmp(c->book_id, b->book_id) != 0) continue;
        printf("    %-12s %-9s %-5s", c->copy_id, copy_state_to_string(c->state), copy_condition_to_string(c->condition));
        if (c->state == COPY_BORROWED) {
            char due[12];
            cli_format_date(c->due_date, due);
            printf(" held by %s, due %s", c->current_holder_id, due);
        }
        printf("\n");
    }
    int waiting = 0;
    for (size_t i = 0; i < s->reservations.count; i++)
        if (strcmp(s->reservations.entries[i].book_id, b->book_id) == 0) waiting++;
    printf("  Reservation queue: %d waiting\n", waiting);
    return 0;
}

static int cmd_add_copy(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "add_copy <book_id> [NEW|GOOD|WORN]")) return 1;
    AppState *s = ctx->state;
    if (!registry_find_book(&s->registry, argv[1])) { printf("Error: unknown book id %s.\n", argv[1]); return 1; }

    CopyCondition cond = COND_NEW;
    if (argc >= 3 && (copy_condition_from_string(argv[2], &cond) != 0 || cond == COND_LOST)) {
        printf("Error: condition must be NEW, GOOD or WORN.\n");
        return 1;
    }
    Copy *c = copy_add(&s->copies, argv[1], cond);
    if (!c) { printf("Error: the copy table is full.\n"); return 1; }
    printf("Added copy %s of %s (%s, AVAILABLE).\n", c->copy_id, c->book_id, copy_condition_to_string(c->condition));
    return 0;
}

static int cmd_view_copy(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "view_copy <copy_id>")) return 1;
    Copy *c = copy_find(&ctx->state->copies, argv[1]);
    if (!c) { printf("Error: unknown copy id %s.\n", argv[1]); return 1; }
    Book *b = registry_find_book(&ctx->state->registry, c->book_id);
    printf("Copy %s of %s (\"%s\")\n  Condition: %s\n  State:     %s\n", c->copy_id, c->book_id,
           b ? b->title : "?", copy_condition_to_string(c->condition), copy_state_to_string(c->state));
    if (c->state == COPY_BORROWED) {
        char due[12];
        cli_format_date(c->due_date, due);
        printf("  Holder:    %s\n  Due:       %s (renewed %d of %d time(s))\n", c->current_holder_id, due,
               c->renewal_count, ctx->state->meta.max_renewals);
    }
    if (c->state == COPY_LOST) printf("  Lost:      reason code %s\n", c->lost_reason_code);
    if (c->state == COPY_RESERVED) printf("  Held at the desk for the next member in the reservation queue.\n");
    return 0;
}

static int cmd_mark_lost(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 2, "mark_lost <copy_id> <reason_code>")) return 1;
    Copy *c = copy_find(&ctx->state->copies, argv[1]);
    if (!c) { printf("Error: unknown copy id %s.\n", argv[1]); return 1; }
    if (!input_valid_id(argv[2], LOST_REASON_LEN)) {
        printf("Error: the reason code must be a short word such as STOLEN, DAMAGED or MISSING.\n");
        return 1;
    }
    int rc = copy_mark_lost(c, argv[2]);
    if (rc == -1) { printf("Error: %s is already marked LOST.\n", argv[1]); return 1; }
    if (rc == -2) { printf("Error: %s is reserved for a waiting member; release the reservation first.\n", argv[1]); return 1; }
    if (rc != 0) { printf("Error: %s cannot be marked lost.\n", argv[1]); return 1; }
    printf("Librarian override: %s is now LOST (reason %s).\n", argv[1], argv[2]);
    return 0;
}

const Command book_commands[] = {
    { "register_book", cmd_register_book, "<book_id> \"<title>\" \"<author>\"", "add a title to the catalogue", 1 },
    { "view_book", cmd_view_book, "<book_id>", "title, copies, availability and reservation queue size", 0 },
    { "add_copy", cmd_add_copy, "<book_id> [NEW|GOOD|WORN]", "add a physical copy of a title", 1 },
    { "view_copy", cmd_view_copy, "<copy_id>", "condition, state, holder and due date of one copy", 0 },
    { "mark_lost", cmd_mark_lost, "<copy_id> <reason_code>", "librarian override: mark a copy LOST", 1 },
    { NULL, NULL, NULL, NULL, 0 }
};
