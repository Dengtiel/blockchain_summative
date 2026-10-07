/* ============================================================
 * cmd_token.c
 * token_transfer, token_balance, utxo_view, account_balance,
 * account_transfer, account_nonce, transaction_history, set_mode,
 * plus the ledger printers shared with the mining commands.
 * ============================================================ */

#include <stdio.h>
#include <string.h>
#include "cli.h"
#include "input.h"
#include "registry.h"

int cli_balance(AppState *s, const char *member_id) {
    return s->meta.ledger_mode == LEDGER_MODE_UTXO ? utxo_balance(&s->utxo_ledger, member_id)
                                                   : account_balance(&s->account_ledger, member_id);
}

static void print_utxo_set(const UTXOLedger *l) {
    int shown = 0, total = 0;
    printf("  %-14s %-8s %s\n", "OWNER", "AMOUNT", "CREATED BY TX");
    for (size_t i = 0; i < l->count; i++) {
        const UTXO *u = &l->outputs[i];
        if (u->spent) continue;
        printf("  %-14s %-8d %.16s...\n", u->owner_id, u->amount, u->tx_id);
        shown++;
        total += u->amount;
    }
    if (!shown) printf("  (no unspent outputs)\n");
    else printf("  %d unspent output(s), %d token(s) in circulation\n", shown, total);
}

static void print_account_table(AccountLedger *l) {
    printf("  %-14s %-9s %s\n", "ACCOUNT", "BALANCE", "NEXT NONCE");
    for (size_t i = 0; i < l->account_count; i++)
        printf("  %-14s %-9d %lu\n", l->accounts[i].member_id, l->accounts[i].balance, l->accounts[i].next_nonce);
    if (!l->account_count) printf("  (no accounts yet)\n");
}

void cli_print_ledger_state(AppState *s) {
    if (s->meta.ledger_mode == LEDGER_MODE_UTXO) {
        printf("UTXO set after this block:\n");
        print_utxo_set(&s->utxo_ledger);
    } else {
        printf("Account balances after this block:\n");
        print_account_table(&s->account_ledger);
    }
}

static int known_member(AppState *s, const char *id) {
    if (!registry_find_member(&s->registry, id)) { printf("Error: unknown member id %s.\n", id); return 0; }
    return 1;
}

static int submit(CliContext *ctx, int argc, char **argv, int has_nonce_arg, int require_account) {
    AppState *s = ctx->state;
    if (require_account && s->meta.ledger_mode != LEDGER_MODE_ACCOUNT) {
        printf("The active model is UTXO. Switch with: set_mode account\n");
        return 1;
    }
    if (!known_member(s, argv[1]) || !known_member(s, argv[2])) return 1;

    long amount;
    if (input_parse_int(argv[3], 1, 1000000, &amount) != 0) { printf("Error: the amount must be a whole number of tokens, at least 1.\n"); return 1; }

    long nonce = 0;
    int has_nonce = 0;
    if (has_nonce_arg && argc >= 5) {
        if (input_parse_int(argv[4], 0, 1000000000L, &nonce) != 0) { printf("Error: the nonce must be a non-negative whole number.\n"); return 1; }
        has_nonce = 1;
    }

    LendResult r;
    if (lending_submit_transfer(s, argv[1], argv[2], (int)amount, TX_FEE, has_nonce, (unsigned long)nonce, 0, &r) != 0) {
        printf("Transfer rejected: %s\n", r.err);
        return 1;
    }
    printf("Transfer %s queued: %ld token(s) from %s to %s, fee %d (%s model", r.request_id, amount, argv[1], argv[2],
           TX_FEE, ledger_mode_name(s->meta.ledger_mode));
    if (s->meta.ledger_mode == LEDGER_MODE_ACCOUNT) printf(", nonce %lu", r.nonce);
    printf(").\n  Balances change when a miner confirms it.\n");
    return 0;
}

static int cmd_token_transfer(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 3, "token_transfer <from_member> <to_member> <amount>")) return 1;
    return submit(ctx, argc, argv, 0, 0);
}

static int cmd_account_transfer(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 3, "account_transfer <from_member> <to_member> <amount> [nonce]")) return 1;
    return submit(ctx, argc, argv, 1, 1);
}

static int cmd_token_balance(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "token_balance <member_id>")) return 1;
    AppState *s = ctx->state;
    if (!known_member(s, argv[1])) return 1;
    printf("%s holds %d token(s) (%s model).\n", argv[1], cli_balance(s, argv[1]), ledger_mode_name(s->meta.ledger_mode));
    if (s->meta.ledger_mode == LEDGER_MODE_UTXO)
        printf("  in %d unspent output(s).\n", utxo_unspent_count(&s->utxo_ledger, argv[1]));
    return 0;
}

static int cmd_utxo_view(CliContext *ctx, int argc, char **argv) {
    (void)argc; (void)argv;
    AppState *s = ctx->state;
    printf("Full UTXO set (active model: %s):\n", ledger_mode_name(s->meta.ledger_mode));
    print_utxo_set(&s->utxo_ledger);
    return 0;
}

static int cmd_account_balance(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "account_balance <member_id>")) return 1;
    AppState *s = ctx->state;
    if (!known_member(s, argv[1])) return 1;
    printf("Account %s: balance %d, next nonce %lu%s\n", argv[1], account_balance(&s->account_ledger, argv[1]),
           account_next_nonce(&s->account_ledger, argv[1]),
           s->meta.ledger_mode == LEDGER_MODE_ACCOUNT ? "" : "  (the UTXO model is active; this account ledger is idle)");
    return 0;
}

static int cmd_account_nonce(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "account_nonce <member_id>")) return 1;
    AppState *s = ctx->state;
    if (!known_member(s, argv[1])) return 1;
    unsigned long confirmed = account_next_nonce(&s->account_ledger, argv[1]);
    int pending = pending_pool_count_transfers(&s->pool, argv[1]);
    printf("%s: next confirmed transfer nonce %lu; %d transfer(s) pending, so a new transfer will carry nonce %lu.\n",
           argv[1], confirmed, pending, confirmed + (unsigned long)pending);
    return 0;
}

static int cmd_transaction_history(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "transaction_history <member_id>")) return 1;
    AppState *s = ctx->state;
    if (!known_member(s, argv[1])) return 1;

    static TxHistoryNode list[256];
    size_t n = account_history_for(&s->account_ledger, argv[1], list, 256);
    printf("Transaction history for %s (newest first, account ledger):\n", argv[1]);
    if (!n) { printf("  (no transactions)\n"); return 0; }
    printf("  %-16s %-10s %-10s %-7s %-4s %-6s %s\n", "TX", "FROM", "TO", "AMOUNT", "FEE", "NONCE", "TIME");
    for (size_t i = 0; i < n; i++) {
        char when[20];
        cli_format_time(list[i].timestamp, when);
        printf("  %.12s...  %-10s %-10s %-7d %-4d %-6lu %s\n", list[i].tx_id, list[i].sender_id, list[i].recipient_id,
               list[i].amount, list[i].fee, list[i].nonce, when);
    }
    return 0;
}

static int cmd_set_mode(CliContext *ctx, int argc, char **argv) {
    if (!cli_need_args(argc, 1, "set_mode <utxo|account>")) return 1;
    AppState *s = ctx->state;
    if (strcmp(argv[1], "utxo") == 0) s->meta.ledger_mode = LEDGER_MODE_UTXO;
    else if (strcmp(argv[1], "account") == 0) s->meta.ledger_mode = LEDGER_MODE_ACCOUNT;
    else { printf("Error: choose 'utxo' or 'account'.\n"); return 1; }
    printf("Token model is now %s. Transfers queued under the other model wait in the pool until it is active again.\n",
           ledger_mode_name(s->meta.ledger_mode));
    return 0;
}

const Command token_commands[] = {
    { "token_transfer", cmd_token_transfer, "<from> <to> <amount>", "queue a token transfer in the active model", 1 },
    { "token_balance", cmd_token_balance, "<member_id>", "token balance in the active model", 0 },
    { "utxo_view", cmd_utxo_view, "", "print the full set of unspent outputs", 0 },
    { "account_balance", cmd_account_balance, "<member_id>", "account-model balance and nonce", 0 },
    { "account_transfer", cmd_account_transfer, "<from> <to> <amount> [nonce]", "account-model transfer with nonce check", 1 },
    { "account_nonce", cmd_account_nonce, "<member_id>", "next expected transfer nonce", 0 },
    { "transaction_history", cmd_transaction_history, "<member_id>", "member's transaction history (linked list)", 0 },
    { "set_mode", cmd_set_mode, "<utxo|account>", "switch the token model for the session", 1 },
    { NULL, NULL, NULL, NULL, 0 }
};
