/* ============================================================
 * persistence.c
 * Whole-chain binary persistence plus the app-level save/load
 * that calls every module's own persistence alongside it.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "persistence.h"
#include "mining.h"
#include "input.h"

#define CHAIN_MAGIC "LBTCHN02"
#define MAX_RECORDS_PER_BLOCK_ON_DISK 4096

/* ---- chain file ---- */

int chain_save(const Chain *chain, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    size_t record_size = sizeof(LendingRecord);
    fwrite(CHAIN_MAGIC, sizeof(CHAIN_MAGIC), 1, f);
    fwrite(&record_size, sizeof(record_size), 1, f);
    fwrite(&chain->length, sizeof(chain->length), 1, f);

    for (Block *b = chain->head; b != NULL; b = b->next) {
        fwrite(&b->index, sizeof(b->index), 1, f);
        fwrite(&b->timestamp, sizeof(b->timestamp), 1, f);
        fwrite(&b->record_count, sizeof(b->record_count), 1, f);
        fwrite(b->previous_hash, sizeof(b->previous_hash), 1, f);
        fwrite(b->merkle_root, sizeof(b->merkle_root), 1, f);
        fwrite(b->hash, sizeof(b->hash), 1, f);
        fwrite(b->sealed_by, sizeof(b->sealed_by), 1, f);
        fwrite(&b->token_reward, sizeof(b->token_reward), 1, f);
        fwrite(b->transaction_id, sizeof(b->transaction_id), 1, f);
        fwrite(&b->pow_nonce, sizeof(b->pow_nonce), 1, f);
        if (b->record_count > 0) fwrite(b->records, sizeof(LendingRecord), b->record_count, f);
    }

    int ok = fflush(f) == 0 && ferror(f) == 0;
    fclose(f);
    return ok ? 0 : -1;
}

/* Reads one block; returns NULL on any short read or implausible size. */
static Block *read_block(FILE *f) {
    Block *b = calloc(1, sizeof(Block));
    if (!b) return NULL;

    int ok = 1;
    ok &= fread(&b->index, sizeof(b->index), 1, f) == 1;
    ok &= fread(&b->timestamp, sizeof(b->timestamp), 1, f) == 1;
    ok &= fread(&b->record_count, sizeof(b->record_count), 1, f) == 1;
    ok &= fread(b->previous_hash, sizeof(b->previous_hash), 1, f) == 1;
    ok &= fread(b->merkle_root, sizeof(b->merkle_root), 1, f) == 1;
    ok &= fread(b->hash, sizeof(b->hash), 1, f) == 1;
    ok &= fread(b->sealed_by, sizeof(b->sealed_by), 1, f) == 1;
    ok &= fread(&b->token_reward, sizeof(b->token_reward), 1, f) == 1;
    ok &= fread(b->transaction_id, sizeof(b->transaction_id), 1, f) == 1;
    ok &= fread(&b->pow_nonce, sizeof(b->pow_nonce), 1, f) == 1;

    if (!ok || b->record_count > MAX_RECORDS_PER_BLOCK_ON_DISK) { free(b); return NULL; }

    /* make sure every text field is terminated even in a damaged file */
    b->previous_hash[HASH_HEX_LEN - 1] = b->merkle_root[HASH_HEX_LEN - 1] = '\0';
    b->hash[HASH_HEX_LEN - 1] = b->transaction_id[HASH_HEX_LEN - 1] = '\0';
    b->sealed_by[SEALER_ID_LEN - 1] = '\0';

    if (b->record_count > 0) {
        b->records = malloc(sizeof(LendingRecord) * b->record_count);
        if (!b->records || fread(b->records, sizeof(LendingRecord), b->record_count, f) != b->record_count) {
            block_free(b);
            return NULL;
        }
    }
    return b;
}

int chain_load(Chain *chain, const char *path) {
    chain_init(chain);

    FILE *f = fopen(path, "rb");
    if (!f) return 1;

    char magic[sizeof(CHAIN_MAGIC)];
    size_t record_size = 0;
    unsigned long length = 0;

    if (fread(magic, sizeof(magic), 1, f) != 1 || memcmp(magic, CHAIN_MAGIC, sizeof(magic)) != 0 ||
        fread(&record_size, sizeof(record_size), 1, f) != 1 || record_size != sizeof(LendingRecord) ||
        fread(&length, sizeof(length), 1, f) != 1 || length == 0 || length > 10000000UL) {
        fclose(f);
        return -1;
    }

    for (unsigned long i = 0; i < length; i++) {
        Block *b = read_block(f);
        if (!b) { fclose(f); chain_free(chain); return -1; }
        chain_append(chain, b);
    }

    fclose(f);
    return 0;
}

/* ---- meta (key=value text) ---- */

static void meta_defaults(ChainMeta *m) {
    m->difficulty = DEFAULT_DIFFICULTY;
    m->block_reward = DEFAULT_BLOCK_REWARD;
    m->ledger_mode = LEDGER_MODE_UTXO;
    m->clock_offset = 0;
    m->loan_days = DEFAULT_LOAN_DAYS;
    m->renewal_days = DEFAULT_RENEWAL_DAYS;
    m->max_renewals = DEFAULT_MAX_RENEWALS;
    m->max_concurrent = DEFAULT_MAX_CONCURRENT;
    m->fine_per_day = DEFAULT_FINE_PER_DAY;
    m->rental_fee = DEFAULT_RENTAL_FEE;
}

static int meta_load(ChainMeta *m, const char *path) {
    meta_defaults(m);
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        input_trim_newline(line);
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        long v = atol(eq + 1);
        const char *k = line;

        if (strcmp(k, "difficulty") == 0 && v >= MIN_DIFFICULTY && v <= MAX_DIFFICULTY) m->difficulty = (int)v;
        else if (strcmp(k, "block_reward") == 0 && v > 0) m->block_reward = (int)v;
        else if (strcmp(k, "ledger_mode") == 0) m->ledger_mode = v == 1 ? LEDGER_MODE_ACCOUNT : LEDGER_MODE_UTXO;
        else if (strcmp(k, "clock_offset") == 0 && v >= 0) m->clock_offset = v;
        else if (strcmp(k, "loan_days") == 0 && v > 0) m->loan_days = (int)v;
        else if (strcmp(k, "renewal_days") == 0 && v > 0) m->renewal_days = (int)v;
        else if (strcmp(k, "max_renewals") == 0 && v >= 0) m->max_renewals = (int)v;
        else if (strcmp(k, "max_concurrent") == 0 && v > 0) m->max_concurrent = (int)v;
        else if (strcmp(k, "fine_per_day") == 0 && v >= 0) m->fine_per_day = (int)v;
        else if (strcmp(k, "rental_fee") == 0 && v >= 0) m->rental_fee = (int)v;
    }
    fclose(f);
    return 0;
}

static int meta_save(const ChainMeta *m, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "difficulty=%d\nblock_reward=%d\nledger_mode=%d\nclock_offset=%ld\n"
               "loan_days=%d\nrenewal_days=%d\nmax_renewals=%d\nmax_concurrent=%d\n"
               "fine_per_day=%d\nrental_fee=%d\n",
            m->difficulty, m->block_reward, (int)m->ledger_mode, m->clock_offset,
            m->loan_days, m->renewal_days, m->max_renewals, m->max_concurrent,
            m->fine_per_day, m->rental_fee);
    fclose(f);
    return 0;
}

/* ---- whole-application state ---- */

static void path_in(char *out, size_t out_size, const AppState *s, const char *name) {
    snprintf(out, out_size, "%s/%s", s->data_dir, name);
}

time_t app_now(const AppState *state) {
    return time(NULL) + (time_t)state->meta.clock_offset;
}

void app_state_init(AppState *state, const char *data_dir) {
    memset(state, 0, sizeof(*state));
    snprintf(state->data_dir, DATA_DIR_LEN, "%s", data_dir);
    meta_defaults(&state->meta);
    chain_init(&state->chain);
}

int app_state_load(AppState *state) {
    char path[DATA_DIR_LEN + 40];
    int problems = 0;

    path_in(path, sizeof(path), state, "meta.txt");
    meta_load(&state->meta, path);

    path_in(path, sizeof(path), state, "chain.dat");
    int rc = chain_load(&state->chain, path);
    if (rc < 0) {
        fprintf(stderr, "ERROR: '%s' is corrupt or from another build; it will not be overwritten.\n", path);
        state->chain_corrupt = 1;
        problems++;
    }
    if (state->chain.length == 0) {
        chain_append(&state->chain, block_create_genesis(app_now(state)));
    }

    path_in(path, sizeof(path), state, "copies.txt");
    copy_list_load(&state->copies, path);
    path_in(path, sizeof(path), state, "reservations.txt");
    reservation_queue_load(&state->reservations, path);
    path_in(path, sizeof(path), state, "pending_pool.dat");
    if (pending_pool_load(&state->pool, path) < 0) problems++;
    path_in(path, sizeof(path), state, "utxo_ledger.txt");
    utxo_ledger_load(&state->utxo_ledger, path);
    path_in(path, sizeof(path), state, "account_ledger.txt");
    account_ledger_load(&state->account_ledger, path);
    path_in(path, sizeof(path), state, "fines.txt");
    fines_load(&state->fines, path);

    char books[DATA_DIR_LEN + 40], members[DATA_DIR_LEN + 40];
    path_in(books, sizeof(books), state, "books.txt");
    path_in(members, sizeof(members), state, "members.txt");
    if (registry_load(&state->registry, books, members) != 0) problems++;

    return problems;
}

int app_state_save(const AppState *state) {
    char path[DATA_DIR_LEN + 40];
    int failures = 0;

    path_in(path, sizeof(path), state, "meta.txt");
    failures += meta_save(&state->meta, path) != 0;

    if (!state->chain_corrupt) {
        path_in(path, sizeof(path), state, "chain.dat");
        failures += chain_save(&state->chain, path) != 0;
    }

    path_in(path, sizeof(path), state, "copies.txt");
    failures += copy_list_save(&state->copies, path) != 0;
    path_in(path, sizeof(path), state, "reservations.txt");
    failures += reservation_queue_save(&state->reservations, path) != 0;
    path_in(path, sizeof(path), state, "pending_pool.dat");
    failures += pending_pool_save(&state->pool, path) != 0;
    path_in(path, sizeof(path), state, "utxo_ledger.txt");
    failures += utxo_ledger_save(&state->utxo_ledger, path) != 0;
    path_in(path, sizeof(path), state, "account_ledger.txt");
    failures += account_ledger_save(&state->account_ledger, path) != 0;
    path_in(path, sizeof(path), state, "fines.txt");
    failures += fines_save(&state->fines, path) != 0;

    char books[DATA_DIR_LEN + 40], members[DATA_DIR_LEN + 40];
    path_in(books, sizeof(books), state, "books.txt");
    path_in(members, sizeof(members), state, "members.txt");
    failures += registry_save(&state->registry, books, members) != 0;

    return failures == 0 ? 0 : -1;
}

void app_state_free(AppState *state) {
    chain_free(&state->chain);
    account_ledger_free(&state->account_ledger);
}
