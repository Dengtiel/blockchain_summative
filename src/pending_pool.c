/* ============================================================
 * pending_pool.c
 * Priority-ordered pool of requests awaiting mining.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "pending_pool.h"

#define POOL_MAGIC "LBTPOOL2"

int pending_pool_load(PendingPool *pool, const char *path) {
    pool->count = 0;
    pool->next_seq = 0;

    FILE *f = fopen(path, "rb");
    if (!f) return 0; /* no saved pool yet */

    char magic[sizeof(POOL_MAGIC)];
    size_t entry_size = 0, count = 0;
    unsigned long next_seq = 0;

    if (fread(magic, sizeof(magic), 1, f) != 1 || memcmp(magic, POOL_MAGIC, sizeof(magic)) != 0 ||
        fread(&entry_size, sizeof(entry_size), 1, f) != 1 || entry_size != sizeof(PendingEntry) ||
        fread(&count, sizeof(count), 1, f) != 1 || count > MAX_PENDING ||
        fread(&next_seq, sizeof(next_seq), 1, f) != 1) {
        fclose(f);
        fprintf(stderr, "WARNING: pending pool file '%s' is unreadable or from another build; starting empty.\n", path);
        return -1;
    }

    size_t read = fread(pool->entries, sizeof(PendingEntry), count, f);
    fclose(f);
    pool->count = read;
    pool->next_seq = next_seq;
    return read == count ? 0 : -1;
}

int pending_pool_save(const PendingPool *pool, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    size_t entry_size = sizeof(PendingEntry);
    fwrite(POOL_MAGIC, sizeof(POOL_MAGIC), 1, f);
    fwrite(&entry_size, sizeof(entry_size), 1, f);
    fwrite(&pool->count, sizeof(pool->count), 1, f);
    fwrite(&pool->next_seq, sizeof(pool->next_seq), 1, f);
    fwrite(pool->entries, sizeof(PendingEntry), pool->count, f);

    fclose(f);
    return 0;
}

int pending_pool_has_request_id(const PendingPool *pool, const char *request_id) {
    for (size_t i = 0; i < pool->count; i++) {
        if (strcmp(pool->entries[i].request_id, request_id) == 0) return 1;
    }
    return 0;
}

static PendingEntry *new_entry(PendingPool *pool, const char *request_id, EntryKind kind,
                               int is_overdue_related, time_t now) {
    if (pool->count >= MAX_PENDING) return NULL;
    if (pending_pool_has_request_id(pool, request_id)) return NULL;

    PendingEntry *e = &pool->entries[pool->count];
    memset(e, 0, sizeof(*e));
    snprintf(e->request_id, REQUEST_ID_LEN, "%s", request_id);
    e->kind = kind;
    e->is_overdue_related = is_overdue_related;
    e->submitted_at = now;
    e->seq = pool->next_seq++;
    e->status = POOL_PENDING;
    pool->count++;
    return e;
}

PendingEntry *pending_pool_add_lending(PendingPool *pool, const char *request_id,
                                       const LendingRecord *record, int is_overdue_related,
                                       time_t reservation_date, time_t now) {
    PendingEntry *e = new_entry(pool, request_id, ENTRY_LENDING, is_overdue_related, now);
    if (!e) return NULL;
    e->record = *record;
    e->reservation_date = reservation_date;
    return e;
}

PendingEntry *pending_pool_add_transfer(PendingPool *pool, const char *request_id,
                                        const TokenTx *tx, int is_overdue_related, time_t now) {
    PendingEntry *e = new_entry(pool, request_id, ENTRY_TRANSFER, is_overdue_related, now);
    if (!e) return NULL;
    e->tx = *tx;
    return e;
}

PendingEntry *pending_pool_find(PendingPool *pool, const char *request_id) {
    for (size_t i = 0; i < pool->count; i++) {
        if (strcmp(pool->entries[i].request_id, request_id) == 0) return &pool->entries[i];
    }
    return NULL;
}

static int entry_cmp(const void *a, const void *b) {
    const PendingEntry *ea = (const PendingEntry *)a;
    const PendingEntry *eb = (const PendingEntry *)b;

    if (ea->is_overdue_related != eb->is_overdue_related) {
        return eb->is_overdue_related - ea->is_overdue_related; /* overdue first */
    }

    /* entries without a reservation sort after any with a real date */
    long ra = ea->reservation_date == 0 ? LONG_MAX : (long)ea->reservation_date;
    long rb = eb->reservation_date == 0 ? LONG_MAX : (long)eb->reservation_date;
    if (ra != rb) return ra < rb ? -1 : 1;

    if (ea->seq != eb->seq) return ea->seq < eb->seq ? -1 : 1; /* arrival order */
    return 0;
}

void pending_pool_sort(PendingPool *pool) {
    qsort(pool->entries, pool->count, sizeof(PendingEntry), entry_cmp);
}

int pending_pool_remove(PendingPool *pool, const char *request_id) {
    for (size_t i = 0; i < pool->count; i++) {
        if (strcmp(pool->entries[i].request_id, request_id) == 0) {
            for (size_t j = i; j + 1 < pool->count; j++) pool->entries[j] = pool->entries[j + 1];
            pool->count--;
            return 0;
        }
    }
    return -1;
}

int pending_pool_mark_suspicious(PendingPool *pool, const char *request_id, const char *reason) {
    PendingEntry *e = pending_pool_find(pool, request_id);
    if (!e) return -1;
    e->status = POOL_SUSPICIOUS;
    snprintf(e->fraud_reason, FRAUD_REASON_LEN, "%s", reason ? reason : "");
    return 0;
}

int pending_pool_mark_confirmed(PendingPool *pool, const char *request_id) {
    PendingEntry *e = pending_pool_find(pool, request_id);
    if (!e) return -1;
    e->status = POOL_CONFIRMED;
    return 0;
}

const char *pending_entry_owner(const PendingEntry *e) {
    return e->kind == ENTRY_LENDING ? e->record.member_id : e->tx.sender;
}

int pending_pool_count_lending(const PendingPool *pool, const char *member_id) {
    int n = 0;
    for (size_t i = 0; i < pool->count; i++) {
        const PendingEntry *e = &pool->entries[i];
        if (e->kind == ENTRY_LENDING && e->status != POOL_CONFIRMED &&
            strcmp(e->record.member_id, member_id) == 0) n++;
    }
    return n;
}

int pending_pool_count_transfers(const PendingPool *pool, const char *sender_id) {
    int n = 0;
    for (size_t i = 0; i < pool->count; i++) {
        const PendingEntry *e = &pool->entries[i];
        if (e->kind == ENTRY_TRANSFER && e->status != POOL_CONFIRMED &&
            strcmp(e->tx.sender, sender_id) == 0) n++;
    }
    return n;
}
