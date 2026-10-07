/* ============================================================
 * cmd_chain.c
 * chain_view, validate_chain, chain_save, chain_load, tamper_demo
 * and advance_time.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"
#include "input.h"

static void print_block_detail(const Block *b) {
    char when[20];
    cli_format_time(b->timestamp, when);
    printf("Block #%lu\n  Time:          %s\n  Sealed by:     %s\n  Difficulty:    %d (pow nonce %lu)\n"
           "  Previous hash: %s\n  Merkle root:   %s\n  Hash:          %s\n  Token reward:  %d\n  Transaction:   %s\n"
           "  Records:       %zu\n",
           b->index, when, b->sealed_by, b->difficulty, b->pow_nonce, b->previous_hash, b->merkle_root, b->hash,
           b->token_reward, b->transaction_id, b->record_count);
    for (size_t i = 0; i < b->record_count; i++) {
        const LendingRecord *r = &b->records[i];
        char due[12] = "-";
        if (r->due_date) cli_format_date(r->due_date, due);
        char leaf[HASH_HEX_LEN];
        record_hash(r, leaf);
        printf("    [%zu] %s %s  book %s copy %s  member %s (%s)\n        due %s, fine %d, nonce %lu, signer %s key %.16s..., signature %u bytes\n"
               "        leaf hash %.32s...\n",
               i + 1, r->record_id, lending_action_to_string(r->action), r->book_id, r->copy_id[0] ? r->copy_id : "-",
               r->member_id, r->member_name, due, r->fine_amount, r->nonce, r->signer_id, r->signer_pubkey, r->sig_len, leaf);
    }
}

static int cmd_chain_view(CliContext *ctx, int argc, char **argv) {
    AppState *s = ctx->state;
    if (argc >= 2) {
        long idx;
        if (input_parse_int(argv[1], 0, (long)s->chain.length - 1, &idx) != 0) {
            printf("Error: block index must be between 0 and %lu.\n", s->chain.length - 1);
            return 1;
        }
        print_block_detail(chain_get(&s->chain, (unsigned long)idx));
        return 0;
    }
    printf("Blockchain: %lu block(s)\n  %-5s %-17s %-5s %-4s %-12s %-6s %s\n", s->chain.length,
           "#", "TIME", "RECS", "DIFF", "SEALED BY", "REWARD", "HASH");
    for (const Block *b = s->chain.head; b; b = b->next) {
        char when[20];
        cli_format_time(b->timestamp, when);
        printf("  %-5lu %-17s %-5zu %-4d %-12s %-6d %.20s...\n", b->index, when, b->record_count, b->difficulty,
               b->sealed_by, b->token_reward, b->hash);
    }
    printf("Use 'chain_view <index>' for a block's header, Merkle root and records.\n");
    return 0;
}

static int cmd_validate_chain(CliContext *ctx, int argc, char **argv) {
    (void)argc; (void)argv;
    AppState *s = ctx->state;
    printf("Validating %lu block(s): hash links, recomputed hashes, proof-of-work, Merkle roots, timestamps, ECDSA signatures, nonces and registry references...\n",
           s->chain.length);
    if (chain_validate(s, 1)) {
        if (s->locked && !s->chain_corrupt) printf("The chain is valid: the session is unlocked.\n");
        s->locked = s->chain_corrupt;
        return 0;
    }
    s->locked = 1;
    printf("The session stays locked: mutating commands are refused until the chain validates.\n");
    return 1;
}

static int cmd_chain_save(CliContext *ctx, int argc, char **argv) {
    (void)argc; (void)argv;
    if (ctx->state->chain_corrupt) { printf("Refused: chain.dat is corrupt and will not be overwritten.\n"); return 1; }
    if (app_state_save(ctx->state) != 0) { printf("Error: some state files could not be written to '%s'.\n", ctx->state->data_dir); return 1; }
    printf("Saved the chain, inventory, reservations, pool, ledgers, fines and settings to '%s'.\n", ctx->state->data_dir);
    return 0;
}

static int cmd_chain_load(CliContext *ctx, int argc, char **argv) {
    (void)argc; (void)argv;
    printf("Reloading everything from '%s' (unsaved changes in memory are discarded)...\n", ctx->state->data_dir);
    int rc = cli_reload(ctx);
    if (rc < 0) { printf("Reload failed.\n"); ctx->running = 0; return 1; }
    return rc;
}

static int cmd_tamper_demo(CliContext *ctx, int argc, char **argv) {
    TamperKind kind = TAMPER_RECORD;
    if (argc >= 2) {
        if (strcmp(argv[1], "record") == 0) kind = TAMPER_RECORD;
        else if (strcmp(argv[1], "block") == 0) kind = TAMPER_BLOCK;
        else if (strcmp(argv[1], "link") == 0) kind = TAMPER_LINK;
        else { printf("Usage: tamper_demo [record|block|link]\n"); return 1; }
    }
    if (chain_tamper_demo(ctx->state, kind) != 0) {
        printf("Nothing to tamper with yet: mine at least one block first.\n");
        return 1;
    }
    return 0;
}

static int cmd_advance_time(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "advance_time <days>")) return 1;
    long days;
    if (input_parse_int(argv[1], 1, 3650, &days) != 0) { printf("Error: advance by 1 to 3650 whole days.\n"); return 1; }
    AppState *s = ctx->state;
    s->meta.clock_offset += days * 24L * 60 * 60;
    char now[20];
    cli_format_time(app_now(s), now);
    printf("System clock advanced by %ld day(s); it is now %s (demo time travel, saved with the chain).\n", days, now);
    int flagged = lending_scan_overdue(s);
    if (flagged) printf("Overdue scan: %d OVERDUE flag(s) queued in the pending pool.\n", flagged);
    return 0;
}

const Command chain_commands[] = {
    { "chain_view", cmd_chain_view, "[block_index]", "list the chain, or show one block with its records", 0 },
    { "validate_chain", cmd_validate_chain, "", "re-verify every block, hash, proof, Merkle root and signature", 0 },
    { "chain_save", cmd_chain_save, "", "write the full state to disk now", 0 },
    { "chain_load", cmd_chain_load, "", "reload the saved state from disk and re-validate", 0 },
    { "tamper_demo", cmd_tamper_demo, "[record|block|link]", "corrupt data in memory, show detection, then revert", 0 },
    { "advance_time", cmd_advance_time, "<days>", "move the system clock forward (to demonstrate overdue loans)", 1 },
    { NULL, NULL, NULL, NULL, 0 }
};
