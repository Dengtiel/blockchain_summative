/* ============================================================
 * cli.c
 * Tokenising, dispatch, help, the read-eval loop and startup.
 * ============================================================ */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"
#include "input.h"
#include "registry.h"

static const Command *const GROUPS[] = {
    member_commands, book_commands, lending_commands, token_commands,
    mining_commands, chain_commands, audit_commands
};
#define GROUP_COUNT (sizeof(GROUPS) / sizeof(GROUPS[0]))

static const char *const GROUP_TITLES[] = {
    "Membership", "Books and copies", "Lending, fines and overdue", "Tokens and ledgers",
    "Pool and mining", "Blockchain", "Fraud and audit"
};

/* ---- helpers shared by the command files ---- */

void cli_format_time(time_t t, char out[20]) {
    struct tm *tm = localtime(&t);
    if (!tm || strftime(out, 20, "%Y-%m-%d %H:%M", tm) == 0) snprintf(out, 20, "-");
}

void cli_format_date(time_t t, char out[12]) {
    struct tm *tm = localtime(&t);
    if (!tm || strftime(out, 12, "%Y-%m-%d", tm) == 0) snprintf(out, 12, "-");
}

int cli_need_args(int argc, int min, const char *usage) {
    if (argc - 1 >= min) return 1;
    printf("Usage: %s\n", usage);
    return 0;
}

/* ---- tokenizer ---- */

int cli_tokenize(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (argc >= max_args) return argc;
        if (*p == '"') {
            argv[argc++] = ++p;
            while (*p && *p != '"') p++;
            if (!*p) return -1;
            *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (*p) *p++ = '\0';
        }
    }
    return argc;
}

/* ---- built-in commands ---- */

static int cmd_help(CliContext *ctx, int argc, char **argv);
static int cmd_exit(CliContext *ctx, int argc, char **argv);

static const Command builtin_commands[] = {
    { "help", cmd_help, "[command]", "list commands, or show one command's usage", 0 },
    { "exit", cmd_exit, "", "save and leave the program", 0 },
    { "quit", cmd_exit, "", "same as exit", 0 },
    { NULL, NULL, NULL, NULL, 0 }
};

static const Command *find_command(const char *name) {
    for (size_t g = 0; g < GROUP_COUNT; g++)
        for (const Command *c = GROUPS[g]; c->name; c++)
            if (strcmp(c->name, name) == 0) return c;
    for (const Command *c = builtin_commands; c->name; c++)
        if (strcmp(c->name, name) == 0) return c;
    return NULL;
}

static int cmd_help(CliContext *ctx, int argc, char **argv) {
    (void)ctx;
    if (argc >= 2) {
        const Command *c = find_command(argv[1]);
        if (!c) { printf("No such command: %s\n", argv[1]); return 1; }
        printf("%s %s\n    %s\n", c->name, c->usage, c->summary);
        return 0;
    }
    for (size_t g = 0; g < GROUP_COUNT; g++) {
        printf("\n%s\n", GROUP_TITLES[g]);
        for (const Command *c = GROUPS[g]; c->name; c++)
            printf("  %-20s %s\n", c->name, c->summary);
    }
    printf("\nOther\n  %-20s %s\n  %-20s %s\n", "help [command]", "this list, or one command's usage",
           "exit", "save and quit");
    printf("\nType 'help <command>' for arguments. Use \"double quotes\" around names with spaces.\n");
    return 0;
}

static int cmd_exit(CliContext *ctx, int argc, char **argv) {
    (void)argc; (void)argv;
    ctx->running = 0;
    return 0;
}

/* ---- dispatch ---- */

int cli_execute(CliContext *ctx, char *line) {
    char *argv[CLI_MAX_ARGS];
    int argc = cli_tokenize(line, argv, CLI_MAX_ARGS);
    if (argc < 0) { printf("Error: unterminated quote.\n"); return 1; }
    if (argc == 0 || argv[0][0] == '#') return 0;

    const Command *cmd = find_command(argv[0]);
    if (!cmd) {
        printf("Unknown command '%s'. Type 'help' for the list.\n", argv[0]);
        return 1;
    }
    if (cmd->mutates && (ctx->state->locked || ctx->state->chain_corrupt)) {
        printf("Refused: the chain is %s. Run validate_chain (or chain_load) before %s.\n",
               ctx->state->chain_corrupt ? "corrupt" : "locked after a failed validation", cmd->name);
        return 1;
    }

    int rc = cmd->fn(ctx, argc, argv);

    if (rc == 0 && cmd->mutates && !ctx->state->chain_corrupt) {
        if (app_state_save(ctx->state) != 0) printf("Warning: some state files could not be saved.\n");
    }
    return rc;
}

void cli_run(CliContext *ctx, FILE *in, int interactive) {
    char line[CLI_LINE_LEN];
    ctx->running = 1;
    while (ctx->running) {
        printf("library> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), in)) { printf("\n"); break; }
        if (strchr(line, '\n') == NULL && !feof(in)) {
            int ch;
            while ((ch = fgetc(in)) != '\n' && ch != EOF) { /* discard the rest */ }
            printf("Error: line too long.\n");
            continue;
        }
        input_trim_newline(line);
        if (!interactive) printf("%s\n", line);    /* echo scripted input so output reads as a session */
        cli_execute(ctx, line);
    }
}

/* ---- startup ---- */

static void ensure_keys(const AppState *s) {
    key_ensure(LIBRARIAN_ID);
    for (size_t i = 0; i < s->registry.member_count; i++) key_ensure(s->registry.members[i].member_id);
}

int cli_boot(CliContext *ctx) {
    AppState *s = ctx->state;
    char books[DATA_DIR_LEN + 24], members[DATA_DIR_LEN + 24], copies[DATA_DIR_LEN + 24], chain[DATA_DIR_LEN + 24];
    snprintf(books, sizeof(books), "%s/books.txt", s->data_dir);
    snprintf(members, sizeof(members), "%s/members.txt", s->data_dir);
    snprintf(copies, sizeof(copies), "%s/copies.txt", s->data_dir);
    snprintf(chain, sizeof(chain), "%s/chain.dat", s->data_dir);

    printf("Loading registries from '%s'...\n", s->data_dir);
    if (registry_check_files(books, members, copies) != 0) {
        printf("Startup aborted: fix the files above (run 'make seed' for sample data).\n");
        return -1;
    }

    FILE *probe = fopen(chain, "rb");
    int had_chain = probe != NULL;
    if (probe) fclose(probe);

    if (app_state_load(s) != 0) printf("Warning: some saved files were unreadable (see messages above).\n");
    if (registry_check_copies(&s->registry, &s->copies) != 0) {
        printf("Startup aborted: copies.txt references books that are not registered.\n");
        return -1;
    }

    ensure_keys(s);
    lending_init(s);
    printf("Registries: %zu book(s), %zu member(s), %zu copy record(s).\n",
           s->registry.book_count, s->registry.member_count, s->copies.count);

    s->locked = 0;
    if (s->chain_corrupt) {
        s->locked = 1;
        printf("LOCKED: chain.dat is corrupt. Restore it from a backup or remove it to start a new chain.\n");
    } else if (had_chain) {
        printf("Validating the chain loaded from disk (%lu block(s))...\n", s->chain.length);
        if (chain_validate(s, 1)) {
            s->locked = 0;
        } else {
            s->locked = 1;
            printf("LOCKED: validation failed. Read-only commands still work; mutating commands are refused.\n");
        }
    } else {
        printf("No saved chain found: started a new chain with a genesis block.\n");
    }

    if (!s->locked) {
        int flagged = lending_scan_overdue(s);
        if (flagged > 0) printf("Overdue scan: %d OVERDUE flag(s) queued in the pending pool.\n", flagged);
        else printf("Overdue scan: nothing overdue.\n");
    }
    return s->locked ? 1 : 0;
}

int cli_reload(CliContext *ctx) {
    char dir[DATA_DIR_LEN];
    snprintf(dir, sizeof(dir), "%s", ctx->state->data_dir);
    app_state_free(ctx->state);
    app_state_init(ctx->state, dir);
    return cli_boot(ctx);
}
