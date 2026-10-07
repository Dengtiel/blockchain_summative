#ifndef CLI_H
#define CLI_H

#include <stdio.h>
#include <time.h>
#include "persistence.h"
#include "lending.h"
#include "confirm.h"
#include "chain_validate.h"
#include "mining.h"

/* ============================================================
 * cli.h
 * The command-line front end. Commands are read one per line
 * (interactively or from a piped script), split into arguments
 * (double quotes keep spaces together), and dispatched through a
 * table. A command flagged `mutates` is refused while the chain is
 * locked, and its success triggers an automatic save to disk.
 * ============================================================ */

#define CLI_MAX_ARGS 12
#define CLI_LINE_LEN 512

typedef struct {
    AppState *state;
    int running;
} CliContext;

typedef int (*CommandFn)(CliContext *ctx, int argc, char **argv);

typedef struct {
    const char *name;
    CommandFn fn;
    const char *usage;      /* argument synopsis, shown by `help` */
    const char *summary;    /* one-line description */
    int mutates;            /* changes saved state: blocked while locked, saved afterwards */
} Command;

/* Each command group lives in its own file and ends with a {NULL} row. */
extern const Command member_commands[];
extern const Command book_commands[];
extern const Command lending_commands[];
extern const Command token_commands[];
extern const Command mining_commands[];
extern const Command chain_commands[];
extern const Command audit_commands[];

/* Splits `line` in place into at most max_args words; "double quotes"
 * group words that contain spaces. Returns the word count, or -1 on an
 * unterminated quote. */
int cli_tokenize(char *line, char *argv[], int max_args);

/* Runs one command line. Returns the command's result (0 = success). */
int cli_execute(CliContext *ctx, char *line);

/* Reads and executes lines until EOF or `exit`. When `echo` is set the
 * prompt and each line are printed (for piped demo scripts). */
void cli_run(CliContext *ctx, FILE *in, int interactive);

/* Loads everything from state->data_dir, validates the chain, locks it
 * if validation fails, and queues OVERDUE flags. Returns 0 if the system
 * is usable, 1 if it came up locked. */
int cli_boot(CliContext *ctx);

/* Frees the current state and boots again from disk. */
int cli_reload(CliContext *ctx);

/* Shared helpers */
void cli_format_time(time_t t, char out[20]);              /* YYYY-MM-DD HH:MM */
void cli_format_date(time_t t, char out[12]);              /* YYYY-MM-DD */
int cli_balance(AppState *s, const char *member_id);        /* in the active ledger */
void cli_print_ledger_state(AppState *s);                   /* UTXO set or account table */
/* Prints "Usage: ..." and returns 0 unless at least `min` arguments follow the command word. */
int cli_need_args(int argc, int min, const char *usage);

#endif
