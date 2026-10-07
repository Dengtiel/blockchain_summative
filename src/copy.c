/* ============================================================
 * copy.c
 * Per-copy inventory with a strict state machine and disk
 * persistence in a pipe-delimited text format.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "copy.h"
#include "input.h"

static const char *CONDITION_NAMES[] = { "NEW", "GOOD", "WORN", "LOST" };
static const char *STATE_NAMES[] = { "AVAILABLE", "BORROWED", "RESERVED", "LOST" };

const char *copy_condition_to_string(CopyCondition c) {
    if (c < 0 || c > COND_LOST) return "UNKNOWN";
    return CONDITION_NAMES[c];
}

const char *copy_state_to_string(CopyState s) {
    if (s < 0 || s > COPY_LOST) return "UNKNOWN";
    return STATE_NAMES[s];
}

int copy_condition_from_string(const char *s, CopyCondition *out) {
    for (int i = 0; i <= COND_LOST; i++) {
        if (strcmp(s, CONDITION_NAMES[i]) == 0) { *out = (CopyCondition)i; return 0; }
    }
    return -1;
}

int copy_state_from_string(const char *s, CopyState *out) {
    for (int i = 0; i <= COPY_LOST; i++) {
        if (strcmp(s, STATE_NAMES[i]) == 0) { *out = (CopyState)i; return 0; }
    }
    return -1;
}

int copy_list_load(CopyList *list, const char *path) {
    list->count = 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[256];
    while (fgets(line, sizeof(line), f) && list->count < MAX_COPIES) {
        input_trim_newline(line);
        if (line[0] == '\0') continue;

        /* Manual pipe-split: sscanf's %[^|] can't match an empty
         * field, which happens whenever current_holder_id or
         * lost_reason_code is empty (most copies have no holder,
         * and only a LOST copy has a reason code). */
        char *fields[8];
        if (input_split_pipe(line, fields, 8) != 8) continue;

        Copy *c = &list->copies[list->count];
        snprintf(c->copy_id, COPY_ID_LEN, "%s", fields[0]);
        snprintf(c->book_id, BOOK_ID_LEN, "%s", fields[1]);
        copy_condition_from_string(fields[2], &c->condition);
        copy_state_from_string(fields[3], &c->state);
        snprintf(c->current_holder_id, MEMBER_ID_LEN, "%s", fields[4]);
        c->due_date = (time_t)atol(fields[5]);
        c->renewal_count = atoi(fields[6]);
        snprintf(c->lost_reason_code, LOST_REASON_LEN, "%s", fields[7]);
        list->count++;
    }
    fclose(f);
    return 0;
}

int copy_list_save(const CopyList *list, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (size_t i = 0; i < list->count; i++) {
        const Copy *c = &list->copies[i];
        fprintf(f, "%s|%s|%s|%s|%s|%ld|%d|%s\n",
                c->copy_id, c->book_id,
                copy_condition_to_string(c->condition),
                copy_state_to_string(c->state),
                c->current_holder_id, (long)c->due_date,
                c->renewal_count, c->lost_reason_code);
    }
    fclose(f);
    return 0;
}

Copy *copy_add(CopyList *list, const char *book_id, CopyCondition condition) {
    if (list->count >= MAX_COPIES) return NULL;

    int existing = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->copies[i].book_id, book_id) == 0) existing++;
    }

    Copy *c = &list->copies[list->count];
    snprintf(c->copy_id, COPY_ID_LEN, "%s-C%d", book_id, existing + 1);
    snprintf(c->book_id, BOOK_ID_LEN, "%s", book_id);
    c->condition = condition;
    c->state = COPY_AVAILABLE;
    c->current_holder_id[0] = '\0';
    c->due_date = 0;
    c->renewal_count = 0;
    c->lost_reason_code[0] = '\0';
    list->count++;
    return c;
}

Copy *copy_find(CopyList *list, const char *copy_id) {
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->copies[i].copy_id, copy_id) == 0) return &list->copies[i];
    }
    return NULL;
}

Copy *copy_find_available(CopyList *list, const char *book_id) {
    for (size_t i = 0; i < list->count; i++) {
        Copy *c = &list->copies[i];
        if (strcmp(c->book_id, book_id) == 0 && c->state == COPY_AVAILABLE) return c;
    }
    return NULL;
}

int copy_count_available(const CopyList *list, const char *book_id) {
    int n = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->copies[i].book_id, book_id) == 0 &&
            list->copies[i].state == COPY_AVAILABLE) n++;
    }
    return n;
}

int copy_count_for_book(const CopyList *list, const char *book_id) {
    int n = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->copies[i].book_id, book_id) == 0) n++;
    }
    return n;
}

int copy_count_borrowed_by(const CopyList *list, const char *member_id) {
    int n = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (list->copies[i].state == COPY_BORROWED &&
            strcmp(list->copies[i].current_holder_id, member_id) == 0) n++;
    }
    return n;
}

int copy_borrow(Copy *c, const char *member_id, time_t due_date) {
    if (c->state != COPY_AVAILABLE && c->state != COPY_RESERVED) return -1;
    c->state = COPY_BORROWED;
    snprintf(c->current_holder_id, MEMBER_ID_LEN, "%s", member_id);
    c->due_date = due_date;
    c->renewal_count = 0;
    return 0;
}

int copy_return(Copy *c) {
    if (c->state != COPY_BORROWED) return -1;
    c->state = COPY_AVAILABLE;
    c->current_holder_id[0] = '\0';
    c->due_date = 0;
    return 0;
}

int copy_reserve(Copy *c) {
    if (c->state != COPY_AVAILABLE) return -1;
    c->state = COPY_RESERVED;
    return 0;
}

int copy_release_reservation(Copy *c) {
    if (c->state != COPY_RESERVED) return -1;
    c->state = COPY_AVAILABLE;
    return 0;
}

int copy_renew(Copy *c, time_t new_due_date, int max_renewals) {
    if (c->state != COPY_BORROWED) return -1;
    if (c->renewal_count >= max_renewals) return -2;
    c->due_date = new_due_date;
    c->renewal_count++;
    return 0;
}

int copy_mark_lost(Copy *c, const char *reason_code) {
    if (c->state == COPY_LOST) return -1;
    if (c->state == COPY_RESERVED) return -2;
    c->state = COPY_LOST;
    c->condition = COND_LOST;
    c->current_holder_id[0] = '\0';
    c->due_date = 0;
    snprintf(c->lost_reason_code, LOST_REASON_LEN, "%s", reason_code);
    return 0;
}
