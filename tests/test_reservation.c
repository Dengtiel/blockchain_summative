/* Unit test: per-title FIFO reservation queue. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reservation.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

int main(void) {
    static ReservationQueue q;
    q.count = 0;

    CHECK(!reservation_has_any(&q, "B001") && reservation_peek_front(&q, "B001") == NULL, "empty queue has no front");
    CHECK(reservation_enqueue(&q, "B001", "M001", 100) == 0, "first reservation is queued");
    CHECK(reservation_enqueue(&q, "B001", "M002", 200) == 0, "second reservation is queued");
    CHECK(reservation_enqueue(&q, "B002", "M003", 150) == 0, "queues are independent per title");
    CHECK(reservation_enqueue(&q, "B001", "M001", 300) == -1, "the same member cannot queue twice for a title");

    Reservation *front = reservation_peek_front(&q, "B001");
    CHECK(front && strcmp(front->member_id, "M001") == 0 && front->reservation_date == 100,
          "front of the queue is the earliest reservation (FIFO)");
    CHECK(reservation_position(&q, "B001", "M002") == 2 && reservation_position(&q, "B001", "M009") == 0,
          "queue positions are 1-based and per title");
    CHECK(reservation_has_member(&q, "B001", "M002") && !reservation_has_member(&q, "B002", "M002"), "membership lookup");
    CHECK(reservation_has_other_than(&q, "B001", "M001") && !reservation_has_other_than(&q, "B002", "M003"),
          "another member's reservation is detected (blocks renewal)");

    CHECK(reservation_dequeue_front(&q, "B001") == 0, "front reservation is consumed");
    front = reservation_peek_front(&q, "B001");
    CHECK(front && strcmp(front->member_id, "M002") == 0, "next member moves to the front");
    CHECK(reservation_peek_front(&q, "B002") != NULL, "other titles are untouched");
    CHECK(reservation_dequeue_front(&q, "B001") == 0 && reservation_dequeue_front(&q, "B001") == -1,
          "dequeue on an empty title fails");

    reservation_enqueue(&q, "B003", "M005", 900);
    const char *path = "/tmp/lbt_reservations_test.txt";
    CHECK(reservation_queue_save(&q, path) == 0, "queue saves to disk");
    static ReservationQueue loaded;
    CHECK(reservation_queue_load(&loaded, path) == 0 && loaded.count == q.count, "queue loads back");
    Reservation *f = reservation_peek_front(&loaded, "B003");
    CHECK(f && strcmp(f->member_id, "M005") == 0 && f->reservation_date == 900, "entries round-trip intact");
    remove(path);

    printf(failures == 0 ? "test_reservation: all checks passed\n" : "test_reservation: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
