/* ============================================================
 * main.c
 * Blockchain-Based Library Book Lending Tracker.
 *
 *   ./library_tracker [data_dir] [--difficulty 1-4] [--mode utxo|account]
 *
 * Commands are read from standard input, so a prepared script can be
 * piped in:  ./library_tracker data < script.txt
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "cli.h"
#include "input.h"

static AppState g_state;      /* large tables: keep them off the stack */

static void usage(const char *prog) {
    printf("Usage: %s [data_dir] [--difficulty 1-4] [--mode utxo|account]\n"
           "  data_dir      folder holding books.txt, members.txt, copies.txt and saved state (default: data)\n"
           "  --difficulty  proof-of-work difficulty for new blocks (%d to %d, default %d)\n"
           "  --mode        token model for the session: utxo (default) or account\n",
           prog, MIN_DIFFICULTY, MAX_DIFFICULTY, DEFAULT_DIFFICULTY);
}

int main(int argc, char **argv) {
    const char *data_dir = "data";
    long difficulty = 0;
    int mode_set = 0;
    LedgerMode mode = LEDGER_MODE_UTXO;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--difficulty") == 0 && i + 1 < argc) {
            if (input_parse_int(argv[++i], MIN_DIFFICULTY, MAX_DIFFICULTY, &difficulty) != 0) {
                fprintf(stderr, "Error: --difficulty must be between %d and %d.\n", MIN_DIFFICULTY, MAX_DIFFICULTY);
                return 2;
            }
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "utxo") == 0) mode = LEDGER_MODE_UTXO;
            else if (strcmp(argv[i], "account") == 0) mode = LEDGER_MODE_ACCOUNT;
            else { fprintf(stderr, "Error: --mode must be 'utxo' or 'account'.\n"); return 2; }
            mode_set = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-' && strcmp(data_dir, "data") == 0 && i == 1) {
            data_dir = argv[i];
        } else {
            fprintf(stderr, "Error: unknown argument '%s'.\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    srand((unsigned)time(NULL));     /* pool-mining hash rates are random */

    app_state_init(&g_state, data_dir);
    char keys[DATA_DIR_LEN + 8];
    snprintf(keys, sizeof(keys), "%s/keys", data_dir);
    if (crypto_init(keys) != 0) {
        fprintf(stderr, "Error: could not prepare the key directory '%s'.\n", keys);
        return 1;
    }

    CliContext ctx = { &g_state, 1 };
    printf("Blockchain-Based Library Book Lending Tracker (ALU)\n");
    int boot = cli_boot(&ctx);
    if (boot < 0) { app_state_free(&g_state); crypto_cleanup(); return 1; }

    if (difficulty) g_state.meta.difficulty = (int)difficulty;
    if (mode_set) g_state.meta.ledger_mode = mode;
    printf("Token model: %s. Difficulty: %d. Type 'help' for commands.\n",
           ledger_mode_name(g_state.meta.ledger_mode), g_state.meta.difficulty);

    cli_run(&ctx, stdin, isatty(STDIN_FILENO));

    if (!g_state.chain_corrupt && app_state_save(&g_state) == 0) printf("State saved to '%s'. Goodbye.\n", data_dir);
    else printf("Goodbye.\n");

    app_state_free(&g_state);
    crypto_cleanup();
    return 0;
}
