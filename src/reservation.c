/* ============================================================
 * reservation.c
 * Per-book FIFO reservation queue with disk persistence.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reservation.h"
#include "input.h"

int reservation_queue_load(ReservationQueue *q, const char *path) {
    q->count = 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[256];
    while (fgets(line, sizeof(line), f) && q->count < MAX_RESERVATIONS) {
        input_trim_newline(line);
        if (line[0] == '\0') continue;

        char *fields[3];
        if (input_split_pipe(line, fields, 3) != 3) continue;
        if (!input_valid_id(fields[0], BOOK_ID_LEN) || !input_valid_id(fields[1], MEMBER_ID_LEN)) continue;

        Reservation *r = &q->entries[q->count];
        snprintf(r->book_id, BOOK_ID_LEN, "%s", fields[0]);
        snprintf(r->member_id, MEMBER_ID_LEN, "%s", fields[1]);
        r->reservation_date = (time_t)atol(fields[2]);
        q->count++;
    }
    fclose(f);
    return 0;
}

int reservation_queue_save(const ReservationQueue *q, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (size_t i = 0; i < q->count; i++) {
        const Reservation *r = &q->entries[i];
        fprintf(f, "%s|%s|%ld\n", r->book_id, r->member_id, (long)r->reservation_date);
    }
    fclose(f);
    return 0;
}

int reservation_enqueue(ReservationQueue *q, const char *book_id, const char *member_id, time_t when) {
    if (q->count >= MAX_RESERVATIONS) return -1;
    if (reservation_has_member(q, book_id, member_id)) return -1;

    Reservation *r = &q->entries[q->count];
    snprintf(r->book_id, BOOK_ID_LEN, "%s", book_id);
    snprintf(r->member_id, MEMBER_ID_LEN, "%s", member_id);
    r->reservation_date = when;
    q->count++;
    return 0;
}

Reservation *reservation_peek_front(ReservationQueue *q, const char *book_id) {
    for (size_t i = 0; i < q->count; i++) {
        if (strcmp(q->entries[i].book_id, book_id) == 0) return &q->entries[i];
    }
    return NULL;
}

int reservation_dequeue_front(ReservationQueue *q, const char *book_id) {
    for (size_t i = 0; i < q->count; i++) {
        if (strcmp(q->entries[i].book_id, book_id) == 0) {
            for (size_t j = i; j + 1 < q->count; j++) q->entries[j] = q->entries[j + 1];
            q->count--;
            return 0;
        }
    }
    return -1;
}

int reservation_has_any(const ReservationQueue *q, const char *book_id) {
    for (size_t i = 0; i < q->count; i++) {
        if (strcmp(q->entries[i].book_id, book_id) == 0) return 1;
    }
    return 0;
}

int reservation_has_member(const ReservationQueue *q, const char *book_id, const char *member_id) {
    for (size_t i = 0; i < q->count; i++) {
        if (strcmp(q->entries[i].book_id, book_id) == 0 &&
            strcmp(q->entries[i].member_id, member_id) == 0) return 1;
    }
    return 0;
}

int reservation_has_other_than(const ReservationQueue *q, const char *book_id, const char *member_id) {
    for (size_t i = 0; i < q->count; i++) {
        if (strcmp(q->entries[i].book_id, book_id) == 0 &&
            strcmp(q->entries[i].member_id, member_id) != 0) return 1;
    }
    return 0;
}

int reservation_position(const ReservationQueue *q, const char *book_id, const char *member_id) {
    int pos = 0;
    for (size_t i = 0; i < q->count; i++) {
        if (strcmp(q->entries[i].book_id, book_id) != 0) continue;
        pos++;
        if (strcmp(q->entries[i].member_id, member_id) == 0) return pos;
    }
    return 0;
}
