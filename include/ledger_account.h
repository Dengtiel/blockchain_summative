#ifndef LEDGER_ACCOUNT_H
#define LEDGER_ACCOUNT_H

#include <stddef.h>
#include <time.h>
#include "record.h"
#include "crypto_utils.h"

/* ============================================================
 * ledger_account.h
 * Account-based token model. Each member has a stored balance and
 * an outgoing-transaction nonce that starts at 0.
 *
 * A transfer is accepted only if its nonce equals the sender's
 * current nonce, which increments with every outgoing transaction;
 * a wrong or reused nonce is rejected, which is what makes replaying
 * an old transfer impossible. Every transaction is also recorded in a
 * per-ledger linked list (newest first) so a member's history can be
 * printed on demand.
 * ============================================================ */

#define MAX_ACCOUNTS 500

typedef struct {
    char member_id[MEMBER_ID_LEN];
    int balance;
    unsigned long next_nonce;
} Account;

typedef struct TxHistoryNode {
    char tx_id[HASH_HEX_LEN];
    char sender_id[MEMBER_ID_LEN];      /* "SYSTEM" for minted rewards */
    char recipient_id[MEMBER_ID_LEN];
    int amount;
    int fee;
    unsigned long nonce;
    time_t timestamp;
    struct TxHistoryNode *next;
} TxHistoryNode;

typedef struct {
    Account accounts[MAX_ACCOUNTS];
    size_t account_count;
    TxHistoryNode *history_head;        /* newest first */
    unsigned long tx_counter;
} AccountLedger;

int account_ledger_load(AccountLedger *ledger, const char *path);
int account_ledger_save(const AccountLedger *ledger, const char *path);
void account_ledger_free(AccountLedger *ledger);

/* Finds member_id's account, creating it (balance 0, nonce 0) if new. */
Account *account_find_or_create(AccountLedger *ledger, const char *member_id);

int account_balance(AccountLedger *ledger, const char *member_id);

/* The nonce the next outgoing transfer from member_id must carry. */
unsigned long account_next_nonce(AccountLedger *ledger, const char *member_id);

/* Mints a reward directly to the account (no nonce: not member-initiated). */
int account_credit_reward(AccountLedger *ledger, const char *member_id, int amount,
                          char out_tx_id[HASH_HEX_LEN], time_t now);

/* Moves amount + fee out of the sender and amount into the recipient.
 * Returns 0 on success, -1 (ledger unchanged) if the nonce is not
 * exactly the sender's next_nonce, the balance is insufficient, or
 * amount <= 0 / fee < 0 / sender == recipient. On success the sender's
 * nonce advances and a history node is prepended. */
int account_transfer(AccountLedger *ledger, const char *sender_id, const char *recipient_id,
                     int amount, int fee, unsigned long nonce, char out_tx_id[HASH_HEX_LEN],
                     time_t now);

/* Copies member_id's transactions (as sender or recipient), newest
 * first, into out_list (up to max_out). Returns how many were written. */
size_t account_history_for(const AccountLedger *ledger, const char *member_id,
                           TxHistoryNode *out_list, size_t max_out);

#endif
