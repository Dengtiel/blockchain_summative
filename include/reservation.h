#ifndef RESERVATION_H
#define RESERVATION_H

#include <stddef.h>
#include <time.h>
#include "record.h"

/* ============================================================
 * reservation.h
 * A first-in-first-out reservation queue per book title. When a
 * copy of a title is returned and a queue exists, the copy is
 * offered to the member at the front of that title's queue before
 * it becomes generally available.
 * ============================================================ */

#define MAX_RESERVATIONS 1000

typedef struct {
    char book_id[BOOK_ID_LEN];
    char member_id[MEMBER_ID_LEN];
    time_t reservation_date;
} Reservation;

typedef struct {
    Reservation entries[MAX_RESERVATIONS];
    size_t count;
} ReservationQueue;

int reservation_queue_load(ReservationQueue *q, const char *path);
int reservation_queue_save(const ReservationQueue *q, const char *path);

/* Appends a reservation made at time `when`. Insertion order is queue
 * order, so the earliest reservation for a title is always the first
 * remaining entry for it. Returns -1 if the member already has a
 * reservation on this title or the queue is full. */
int reservation_enqueue(ReservationQueue *q, const char *book_id, const char *member_id, time_t when);

/* The earliest pending reservation for book_id, or NULL if none. */
Reservation *reservation_peek_front(ReservationQueue *q, const char *book_id);

/* Removes the earliest reservation for book_id, preserving FIFO order
 * of the rest. Returns 0 on success, -1 if there was none. */
int reservation_dequeue_front(ReservationQueue *q, const char *book_id);

int reservation_has_any(const ReservationQueue *q, const char *book_id);

/* True if member_id already waits in the queue for book_id. */
int reservation_has_member(const ReservationQueue *q, const char *book_id, const char *member_id);

/* True if somebody other than member_id waits for book_id (this is what
 * blocks a renewal). */
int reservation_has_other_than(const ReservationQueue *q, const char *book_id, const char *member_id);

/* 1-based position of member_id in book_id's queue, or 0 if absent. */
int reservation_position(const ReservationQueue *q, const char *book_id, const char *member_id);

#endif
