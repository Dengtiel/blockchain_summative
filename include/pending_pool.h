#ifndef PENDING_POOL_H
#define PENDING_POOL_H

#include <stddef.h>
#include <time.h>
#include "record.h"
#include "copy.h"

/* ============================================================
 * pending_pool.h
 * Every lending action and token transfer waits here until a
 * miner confirms it into a block. Entries are served in priority
 * order:
 *
 *   1. overdue-related requests first (late returns, OVERDUE flags,
 *      fine settlements),
 *   2. then by reservation date, ascending (whoever waited longest),
 *   3. then by arrival order.
 *
 * A request that trips a fraud heuristic is stored with status
 * SUSPICIOUS; mining never selects it until a librarian approves it.
 * ============================================================ */

#define MAX_PENDING      500
#define REQUEST_ID_LEN   32
#define FRAUD_REASON_LEN 160

typedef enum {
    POOL_PENDING,
    POOL_CONFIRMED,
    POOL_SUSPICIOUS
} PoolStatus;

typedef enum {
    ENTRY_LENDING,      /* becomes a LendingRecord inside a block */
    ENTRY_TRANSFER      /* a token transfer applied to the active ledger when mined */
} EntryKind;

typedef struct {
    char sender[MEMBER_ID_LEN];
    char recipient[MEMBER_ID_LEN];
    int amount;
    int fee;                    /* paid to whoever mines the block */
    unsigned long nonce;        /* account model: sender's expected nonce */
    int ledger_mode;            /* LedgerMode in force when it was submitted */
    int is_fine;                /* a fine settlement (credited to the fine book) */
} TokenTx;

typedef struct {
    char request_id[REQUEST_ID_LEN];
    EntryKind kind;
    LendingRecord record;       /* valid when kind == ENTRY_LENDING */
    TokenTx tx;                 /* valid when kind == ENTRY_TRANSFER */
    int is_overdue_related;
    time_t reservation_date;    /* 0 when the request involves no reservation */
    time_t submitted_at;
    unsigned long seq;          /* arrival order, the final tie-break */
    int has_condition;          /* a return that reports the copy's condition */
    CopyCondition new_condition;
    PoolStatus status;
    char fraud_reason[FRAUD_REASON_LEN];
} PendingEntry;

typedef struct {
    PendingEntry entries[MAX_PENDING];
    size_t count;
    unsigned long next_seq;
} PendingPool;

/* The pool is stored as a binary image (it holds fixed-size signature
 * bytes). The file starts with a magic string and the entry size so a
 * file from an incompatible build is detected instead of misread. */
int pending_pool_load(PendingPool *pool, const char *path);
int pending_pool_save(const PendingPool *pool, const char *path);

/* Both adders return the new entry, or NULL if the pool is full or the
 * request_id is already present. New entries start PENDING. */
PendingEntry *pending_pool_add_lending(PendingPool *pool, const char *request_id,
                                       const LendingRecord *record, int is_overdue_related,
                                       time_t reservation_date, time_t now);
PendingEntry *pending_pool_add_transfer(PendingPool *pool, const char *request_id,
                                        const TokenTx *tx, int is_overdue_related, time_t now);

PendingEntry *pending_pool_find(PendingPool *pool, const char *request_id);
int pending_pool_has_request_id(const PendingPool *pool, const char *request_id);

/* Sorts into the priority order described above. */
void pending_pool_sort(PendingPool *pool);

/* Removes an entry (mined, rejected, or discarded). Returns -1 if absent. */
int pending_pool_remove(PendingPool *pool, const char *request_id);

int pending_pool_mark_suspicious(PendingPool *pool, const char *request_id, const char *reason);
int pending_pool_mark_confirmed(PendingPool *pool, const char *request_id);

/* The member a lending entry belongs to, or the sender of a transfer. */
const char *pending_entry_owner(const PendingEntry *e);

/* How many not-yet-mined LENDING requests member_id has in the pool
 * (PENDING or SUSPICIOUS) -- used to assign consecutive nonces. */
int pending_pool_count_lending(const PendingPool *pool, const char *member_id);

/* How many not-yet-mined transfers sender_id has in the pool. */
int pending_pool_count_transfers(const PendingPool *pool, const char *sender_id);

#endif
