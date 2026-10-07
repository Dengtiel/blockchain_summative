/* ============================================================
 * ledger_account.c
 * Account-based token model with nonce replay protection and a
 * linked-list transaction history.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ledger_account.h"
#include "ledger_common.h"
#include "input.h"

Account *account_find_or_create(AccountLedger *ledger, const char *member_id) {
    for (size_t i = 0; i < ledger->account_count; i++) {
        if (strcmp(ledger->accounts[i].member_id, member_id) == 0) return &ledger->accounts[i];
    }
    if (ledger->account_count >= MAX_ACCOUNTS) return NULL;

    Account *a = &ledger->accounts[ledger->account_count++];
    snprintf(a->member_id, MEMBER_ID_LEN, "%s", member_id);
    a->balance = 0;
    a->next_nonce = 0;
    return a;
}

int account_balance(AccountLedger *ledger, const char *member_id) {
    Account *a = account_find_or_create(ledger, member_id);
    return a ? a->balance : 0;
}

unsigned long account_next_nonce(AccountLedger *ledger, const char *member_id) {
    Account *a = account_find_or_create(ledger, member_id);
    return a ? a->next_nonce : 0;
}

static TxHistoryNode *make_node(const char *tx_id, const char *sender_id, const char *recipient_id,
                                int amount, int fee, unsigned long nonce, time_t timestamp) {
    TxHistoryNode *node = malloc(sizeof(TxHistoryNode));
    if (!node) return NULL;

    snprintf(node->tx_id, HASH_HEX_LEN, "%s", tx_id);
    snprintf(node->sender_id, MEMBER_ID_LEN, "%s", sender_id);
    snprintf(node->recipient_id, MEMBER_ID_LEN, "%s", recipient_id);
    node->amount = amount;
    node->fee = fee;
    node->nonce = nonce;
    node->timestamp = timestamp;
    node->next = NULL;
    return node;
}

static void history_prepend(AccountLedger *ledger, TxHistoryNode *node) {
    if (!node) return;
    node->next = ledger->history_head;
    ledger->history_head = node;
}

int account_credit_reward(AccountLedger *ledger, const char *member_id, int amount,
                          char out_tx_id[HASH_HEX_LEN], time_t now) {
    if (amount <= 0) return -1;
    Account *a = account_find_or_create(ledger, member_id);
    if (!a) return -1;

    char seed[128];
    snprintf(seed, sizeof(seed), "REWARD:%s:%d", member_id, amount);
    tx_generate_id(seed, ledger->tx_counter++, out_tx_id);

    a->balance += amount;
    history_prepend(ledger, make_node(out_tx_id, "SYSTEM", member_id, amount, 0, 0, now));
    return 0;
}

int account_transfer(AccountLedger *ledger, const char *sender_id, const char *recipient_id,
                     int amount, int fee, unsigned long nonce, char out_tx_id[HASH_HEX_LEN],
                     time_t now) {
    if (amount <= 0 || fee < 0 || strcmp(sender_id, recipient_id) == 0) return -1;

    Account *sender = account_find_or_create(ledger, sender_id);
    if (!sender) return -1;

    if (nonce != sender->next_nonce) return -1;      /* replayed, reused or out-of-order nonce */
    if (sender->balance < amount + fee) return -1;   /* insufficient balance */

    Account *recipient = account_find_or_create(ledger, recipient_id);
    if (!recipient) return -1;

    sender->balance -= (amount + fee);
    recipient->balance += amount;
    sender->next_nonce++;

    char seed[160];
    snprintf(seed, sizeof(seed), "XFER:%s:%s:%d:%lu", sender_id, recipient_id, amount, nonce);
    tx_generate_id(seed, ledger->tx_counter++, out_tx_id);

    history_prepend(ledger, make_node(out_tx_id, sender_id, recipient_id, amount, fee, nonce, now));
    return 0;
}

size_t account_history_for(const AccountLedger *ledger, const char *member_id,
                           TxHistoryNode *out_list, size_t max_out) {
    size_t written = 0;
    for (TxHistoryNode *node = ledger->history_head; node != NULL && written < max_out; node = node->next) {
        if (strcmp(node->sender_id, member_id) == 0 || strcmp(node->recipient_id, member_id) == 0) {
            out_list[written] = *node;
            out_list[written].next = NULL;
            written++;
        }
    }
    return written;
}

void account_ledger_free(AccountLedger *ledger) {
    TxHistoryNode *node = ledger->history_head;
    while (node) {
        TxHistoryNode *next = node->next;
        free(node);
        node = next;
    }
    ledger->history_head = NULL;
}

int account_ledger_load(AccountLedger *ledger, const char *path) {
    ledger->account_count = 0;
    ledger->history_head = NULL;
    ledger->tx_counter = 0;

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[320];
    if (fgets(line, sizeof(line), f)) ledger->tx_counter = strtoul(line, NULL, 10);

    TxHistoryNode *tail = NULL; /* the file lists history newest first, so append at the tail */

    while (fgets(line, sizeof(line), f)) {
        input_trim_newline(line);
        if (line[0] == '\0') continue;

        char *fld[8];
        int n = input_split_pipe(line, fld, 8);

        if (line[0] == 'A' && n == 4 && ledger->account_count < MAX_ACCOUNTS) {
            Account *a = &ledger->accounts[ledger->account_count++];
            snprintf(a->member_id, MEMBER_ID_LEN, "%s", fld[1]);
            a->balance = atoi(fld[2]);
            a->next_nonce = strtoul(fld[3], NULL, 10);
        } else if (line[0] == 'T' && n == 8) {
            TxHistoryNode *node = make_node(fld[1], fld[2], fld[3], atoi(fld[4]), atoi(fld[5]),
                                            strtoul(fld[6], NULL, 10), (time_t)atol(fld[7]));
            if (!node) continue;
            if (tail) tail->next = node; else ledger->history_head = node;
            tail = node;
        }
    }
    fclose(f);
    return 0;
}

int account_ledger_save(const AccountLedger *ledger, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "%lu\n", ledger->tx_counter);

    for (size_t i = 0; i < ledger->account_count; i++) {
        const Account *a = &ledger->accounts[i];
        fprintf(f, "A|%s|%d|%lu\n", a->member_id, a->balance, a->next_nonce);
    }

    for (TxHistoryNode *node = ledger->history_head; node != NULL; node = node->next) {
        fprintf(f, "T|%s|%s|%s|%d|%d|%lu|%ld\n",
                node->tx_id, node->sender_id, node->recipient_id,
                node->amount, node->fee, node->nonce, (long)node->timestamp);
    }

    fclose(f);
    return 0;
}
