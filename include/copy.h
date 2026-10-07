#ifndef COPY_H
#define COPY_H

#include <stddef.h>
#include <time.h>
#include "record.h"

/* ============================================================
 * copy.h
 * A single physical copy of a book title, with its own condition
 * and a strict state machine:
 *   AVAILABLE -> BORROWED   (copy_borrow)
 *   BORROWED  -> AVAILABLE  (copy_return)
 *   AVAILABLE -> RESERVED   (copy_reserve)
 *   RESERVED  -> AVAILABLE  (copy_release_reservation)
 *   RESERVED  -> BORROWED   (copy_borrow, offered copy)
 *   AVAILABLE or BORROWED -> LOST (copy_mark_lost; needs a reason code; terminal)
 *
 * Callers never assign Copy.state directly: every change goes through
 * one of the transition functions below, which reject illegal moves.
 * ============================================================ */

#define MAX_COPIES        1000
#define LOST_REASON_LEN   24

typedef enum {
    COND_NEW,
    COND_GOOD,
    COND_WORN,
    COND_LOST
} CopyCondition;

typedef enum {
    COPY_AVAILABLE,
    COPY_BORROWED,
    COPY_RESERVED,
    COPY_LOST
} CopyState;

typedef struct {
    char copy_id[COPY_ID_LEN];
    char book_id[BOOK_ID_LEN];
    CopyCondition condition;
    CopyState state;
    char current_holder_id[MEMBER_ID_LEN];
    time_t due_date;
    int renewal_count;                   /* resets to 0 on each new borrow */
    char lost_reason_code[LOST_REASON_LEN]; /* set only when state == COPY_LOST */
} Copy;

typedef struct {
    Copy copies[MAX_COPIES];
    size_t count;
} CopyList;

const char *copy_condition_to_string(CopyCondition c);
const char *copy_state_to_string(CopyState s);
int copy_condition_from_string(const char *s, CopyCondition *out);
int copy_state_from_string(const char *s, CopyState *out);

int copy_list_load(CopyList *list, const char *path);
int copy_list_save(const CopyList *list, const char *path);

/* Adds a new AVAILABLE copy, generating its copy_id as
 * "<book_id>-C<n>" from the count of existing copies of that book. */
Copy *copy_add(CopyList *list, const char *book_id, CopyCondition condition);

Copy *copy_find(CopyList *list, const char *copy_id);
Copy *copy_find_available(CopyList *list, const char *book_id);
int copy_count_available(const CopyList *list, const char *book_id);

/* Total number of physical copies registered for a title (any state). */
int copy_count_for_book(const CopyList *list, const char *book_id);

/* Number of copies currently BORROWED by member_id. */
int copy_count_borrowed_by(const CopyList *list, const char *member_id);

int copy_borrow(Copy *c, const char *member_id, time_t due_date);
int copy_return(Copy *c);
int copy_reserve(Copy *c);
int copy_release_reservation(Copy *c);

/* Extends due_date, provided the copy is BORROWED and has not
 * already been renewed max_renewals times. Returns -1 if the copy
 * is not borrowed, -2 if the renewal cap has been reached. */
int copy_renew(Copy *c, time_t new_due_date, int max_renewals);

/* Librarian override: marks an AVAILABLE or BORROWED copy LOST and
 * records the reason code. Returns -1 if the copy is already LOST and
 * -2 if it is RESERVED (a reserved copy must be released first). */
int copy_mark_lost(Copy *c, const char *reason_code);

#endif
