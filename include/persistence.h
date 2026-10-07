#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <stddef.h>
#include <time.h>
#include "blockchain.h"
#include "copy.h"
#include "reservation.h"
#include "pending_pool.h"
#include "ledger_utxo.h"
#include "ledger_account.h"
#include "ledger_common.h"
#include "registry.h"
#include "fines.h"

/* ============================================================
 * persistence.h
 * Ties every module's on-disk state together under one data
 * directory so the whole system survives a restart.
 *
 *   chain.dat          binary: header + every block and its records
 *   pending_pool.dat   binary: header + the pool's entries
 *   copies.txt         text:   one copy per line (also a startup input)
 *   reservations.txt   text:   book|member|date
 *   utxo_ledger.txt    text:   tx counter, then one output per line
 *   account_ledger.txt text:   tx counter, accounts, then history
 *   fines.txt          text:   member|settled
 *   meta.txt           text:   key=value (difficulty, reward, ledger
 *                              mode, clock offset, library policies)
 *   books.txt / members.txt    text registries (see registry.h)
 *   keys/              ECDSA key pairs (see crypto_utils.h)
 *
 * Binary files start with a magic string and the struct size, so a
 * file written by an incompatible build is detected, not misread.
 * ============================================================ */

#define DEFAULT_BLOCK_REWARD   50
#define DEFAULT_LOAN_DAYS      14
#define DEFAULT_RENEWAL_DAYS   7
#define DEFAULT_MAX_RENEWALS   2
#define DEFAULT_MAX_CONCURRENT 3
#define DEFAULT_FINE_PER_DAY   1
#define DEFAULT_RENTAL_FEE     20
#define DATA_DIR_LEN           256

typedef struct {
    int difficulty;
    int block_reward;
    LedgerMode ledger_mode;
    long clock_offset;          /* seconds added to the wall clock (demo time travel) */
    int loan_days;
    int renewal_days;
    int max_renewals;
    int max_concurrent;
    int fine_per_day;
    int rental_fee;             /* cloud mining: tokens per rented round */
} ChainMeta;

typedef struct {
    Chain chain;
    CopyList copies;
    ReservationQueue reservations;
    PendingPool pool;
    UTXOLedger utxo_ledger;
    AccountLedger account_ledger;
    FineBook fines;
    Registry registry;
    ChainMeta meta;

    /* runtime-only fields (never written to disk) */
    char data_dir[DATA_DIR_LEN];
    int chain_corrupt;          /* chain.dat was unreadable: it must not be overwritten */
    int locked;                 /* the loaded chain has not been validated yet */
} AppState;

/* Chain file: -1 corrupt/unreadable, 1 no file yet (chain left empty), 0 loaded. */
int chain_save(const Chain *chain, const char *path);
int chain_load(Chain *chain, const char *path);

/* Resets `state` to an empty system with default settings. */
void app_state_init(AppState *state, const char *data_dir);

/* Loads every piece of state from state->data_dir. Missing optional
 * files start empty; a missing chain starts a new genesis block; an
 * unreadable chain sets chain_corrupt. Returns the number of problems. */
int app_state_load(AppState *state);

/* Writes every piece of state back (the chain only if it was not corrupt). */
int app_state_save(const AppState *state);

void app_state_free(AppState *state);

/* Current time as the system sees it: wall clock plus meta.clock_offset. */
time_t app_now(const AppState *state);

#endif
