#ifndef FINES_H
#define FINES_H

#include <stddef.h>
#include "record.h"

/* ============================================================
 * fines.h
 * Tracks how many tokens of fines each member has already settled.
 * What a member owes is derived from the chain: the fine_amount on
 * their late RETURNED records. Outstanding = assessed - settled, so
 * the chain stays the single source of truth for what was assessed
 * and this small book only records what has been paid.
 * ============================================================ */

#define MAX_FINE_ACCOUNTS 500

typedef struct {
    char member_id[MEMBER_ID_LEN];
    int settled;
} FineEntry;

typedef struct {
    FineEntry entries[MAX_FINE_ACCOUNTS];
    size_t count;
} FineBook;

int fines_load(FineBook *book, const char *path);
int fines_save(const FineBook *book, const char *path);

int fines_settled(const FineBook *book, const char *member_id);

/* Records `amount` more tokens of fines paid by member_id. */
int fines_add_settled(FineBook *book, const char *member_id, int amount);

#endif
