/* ============================================================
 * cmd_member.c
 * register_member, view_member, member_nonce.
 * ============================================================ */

#include <stdio.h>
#include <string.h>
#include "cli.h"
#include "input.h"
#include "registry.h"

static int is_reserved_id(const char *id) {
    return strcmp(id, LIBRARIAN_ID) == 0 || strcmp(id, LIBRARY_ACCOUNT) == 0;
}

static int cmd_register_member(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 3, "register_member <member_id> \"<full name>\" <course_code>")) return 1;
    AppState *s = ctx->state;

    if (!input_valid_id(argv[1], MEMBER_ID_LEN) || is_reserved_id(argv[1])) {
        printf("Error: invalid member id '%s' (letters, digits, '-' or '_', up to %d characters).\n", argv[1], MEMBER_ID_LEN - 1);
        return 1;
    }
    if (!input_valid_text(argv[2], NAME_LEN)) { printf("Error: invalid name (printable, no '|', under %d characters).\n", NAME_LEN); return 1; }
    if (!input_valid_id(argv[3], COURSE_CODE_LEN)) { printf("Error: invalid course code '%s'.\n", argv[3]); return 1; }

    if (registry_add_member(&s->registry, argv[1], argv[2], argv[3]) != 0) {
        printf("Error: member %s already exists or the registry is full.\n", argv[1]);
        return 1;
    }
    char pub[PUBKEY_HEX_LEN];
    if (key_ensure(argv[1]) != 0 || key_get_public_hex(argv[1], pub) != 0) {
        printf("Error: could not create a signing key for %s.\n", argv[1]);
        return 1;
    }
    printf("Registered %s (%s, %s).\nOn-chain identity (ECDSA P-256 public key): %.24s...\n",
           argv[1], argv[2], argv[3], pub);
    return 0;
}

static int cmd_view_member(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "view_member <member_id>")) return 1;
    AppState *s = ctx->state;
    Member *m = registry_find_member(&s->registry, argv[1]);
    if (!m) { printf("Error: unknown member id %s.\n", argv[1]); return 1; }

    char pub[PUBKEY_HEX_LEN] = "-";
    key_get_public_hex(m->member_id, pub);
    printf("Member %s\n  Name:        %s\n  Course:      %s\n  Public key:  %.32s...\n",
           m->member_id, m->full_name, m->course_code, pub);
    printf("  Tokens:      %d (%s model)\n", cli_balance(s, m->member_id), ledger_mode_name(s->meta.ledger_mode));
    printf("  Nonce:       %lu confirmed, %d pending\n", lending_chain_nonce(s, m->member_id),
           pending_pool_count_lending(&s->pool, m->member_id));

    FineSummary fs;
    lending_fine_summary(s, m->member_id, &fs);
    printf("  Fines:       assessed %d, settled %d, pending settlement %d, outstanding %d\n",
           fs.assessed, fs.settled, fs.pending, fs.outstanding);

    int held = 0;
    for (size_t i = 0; i < s->copies.count; i++) {
        const Copy *c = &s->copies.copies[i];
        if (c->state != COPY_BORROWED || strcmp(c->current_holder_id, m->member_id) != 0) continue;
        char due[12];
        cli_format_date(c->due_date, due);
        printf("  %s %s (%s), due %s%s\n", held == 0 ? "Holding:    " : "            ", c->copy_id, c->book_id, due,
               app_now(s) > c->due_date ? "  OVERDUE" : "");
        held++;
    }
    if (!held) printf("  Holding:     nothing\n");
    return 0;
}

static int cmd_member_nonce(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "member_nonce <member_id>")) return 1;
    AppState *s = ctx->state;
    if (!registry_find_member(&s->registry, argv[1])) { printf("Error: unknown member id %s.\n", argv[1]); return 1; }
    unsigned long confirmed = lending_chain_nonce(s, argv[1]);
    int pending = pending_pool_count_lending(&s->pool, argv[1]);
    printf("%s: %lu record(s) confirmed on the chain, %d waiting in the pool.\n"
           "Next confirmed record must carry nonce %lu; the next new request will carry nonce %lu.\n",
           argv[1], confirmed, pending, confirmed, confirmed + (unsigned long)pending);
    return 0;
}

const Command member_commands[] = {
    { "register_member", cmd_register_member, "<member_id> \"<full name>\" <course_code>", "add a member and generate their signing key", 1 },
    { "view_member", cmd_view_member, "<member_id>", "profile, identity key, tokens, nonce, fines and loans", 0 },
    { "member_nonce", cmd_member_nonce, "<member_id>", "confirmed and pending nonce for a member", 0 },
    { NULL, NULL, NULL, NULL, 0 }
};
