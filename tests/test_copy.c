/* Unit test: copy inventory state machine and copies.txt persistence. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "copy.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

int main(void) {
    static CopyList list;
    list.count = 0;

    Copy *c1 = copy_add(&list, "B001", COND_NEW);
    Copy *c2 = copy_add(&list, "B001", COND_GOOD);
    Copy *c3 = copy_add(&list, "B002", COND_WORN);
    CHECK(strcmp(c1->copy_id, "B001-C1") == 0 && strcmp(c2->copy_id, "B001-C2") == 0 &&
          strcmp(c3->copy_id, "B002-C1") == 0, "copy ids are generated per title");
    CHECK(copy_count_for_book(&list, "B001") == 2 && copy_count_available(&list, "B001") == 2,
          "a new title with two copies has two available");
    CHECK(c1->state == COPY_AVAILABLE && c1->current_holder_id[0] == '\0', "new copies start AVAILABLE with no holder");

    /* AVAILABLE -> BORROWED */
    CHECK(copy_borrow(c1, "M001", 5000) == 0 && c1->state == COPY_BORROWED, "AVAILABLE -> BORROWED on borrow");
    CHECK(strcmp(c1->current_holder_id, "M001") == 0 && c1->due_date == 5000, "borrow records holder and due date");
    CHECK(copy_count_available(&list, "B001") == 1, "title is partially available while one copy is out");
    CHECK(copy_count_borrowed_by(&list, "M001") == 1, "borrowed-by count");
    CHECK(copy_borrow(c1, "M002", 6000) != 0, "a BORROWED copy cannot be borrowed again");
    CHECK(copy_reserve(c1) != 0, "a BORROWED copy cannot be reserved directly");

    /* renewals */
    CHECK(copy_renew(c1, 7000, 2) == 0 && c1->renewal_count == 1 && c1->due_date == 7000, "first renewal extends the due date");
    CHECK(copy_renew(c1, 8000, 2) == 0 && c1->renewal_count == 2, "second renewal is allowed");
    CHECK(copy_renew(c1, 9000, 2) == -2 && c1->due_date == 8000, "third renewal hits the cap and changes nothing");
    CHECK(copy_renew(c2, 9000, 2) == -1, "an unborrowed copy cannot be renewed");

    /* BORROWED -> AVAILABLE or RESERVED */
    CHECK(copy_return(c1) == 0 && c1->state == COPY_AVAILABLE && c1->current_holder_id[0] == '\0',
          "BORROWED -> AVAILABLE on return");
    CHECK(copy_return(c1) != 0, "an AVAILABLE copy cannot be returned");
    CHECK(copy_reserve(c1) == 0 && c1->state == COPY_RESERVED, "returned copy goes AVAILABLE -> RESERVED when a queue exists");
    CHECK(copy_borrow(c1, "M003", 9000) == 0 && c1->renewal_count == 0, "reserved copy can be borrowed and renewals reset");
    copy_return(c1);
    copy_reserve(c1);
    CHECK(copy_release_reservation(c1) == 0 && c1->state == COPY_AVAILABLE, "RESERVED -> AVAILABLE when the offer is released");

    /* LOST */
    CHECK(copy_mark_lost(c2, "WATER_DAMAGE") == 0 && c2->state == COPY_LOST && c2->condition == COND_LOST,
          "AVAILABLE -> LOST with a reason code");
    CHECK(strcmp(c2->lost_reason_code, "WATER_DAMAGE") == 0, "reason code is stored");
    CHECK(copy_mark_lost(c2, "AGAIN") == -1, "a LOST copy cannot be lost again");
    CHECK(copy_borrow(c2, "M001", 1) != 0 && copy_reserve(c2) != 0, "a LOST copy can never be lent or reserved");
    copy_borrow(c3, "M002", 4000);
    CHECK(copy_mark_lost(c3, "NOT_RETURNED") == 0 && c3->current_holder_id[0] == '\0', "BORROWED -> LOST clears the holder");
    copy_return(c1);
    copy_reserve(c1);
    CHECK(copy_mark_lost(c1, "X") == -2, "a RESERVED copy cannot be marked lost until released");

    /* lookups */
    CHECK(copy_find(&list, "B002-C1") == c3 && copy_find(&list, "NOPE") == NULL, "copy_find");
    CHECK(copy_find_available(&list, "B001") == NULL, "no available copy once all are lost/reserved");
    CopyCondition cc;
    CHECK(copy_condition_from_string("WORN", &cc) == 0 && cc == COND_WORN && copy_condition_from_string("BAD", &cc) != 0,
          "condition parsing");

    /* persistence */
    copy_borrow(c1, "M009", 12345);
    const char *path = "/tmp/lbt_copies_test.txt";
    CHECK(copy_list_save(&list, path) == 0, "copies save to disk");
    static CopyList loaded;
    CHECK(copy_list_load(&loaded, path) == 0 && loaded.count == list.count, "copies load back");
    Copy *r1 = copy_find(&loaded, "B001-C1"), *r2 = copy_find(&loaded, "B001-C2");
    CHECK(r1 && r1->state == COPY_BORROWED && strcmp(r1->current_holder_id, "M009") == 0 && r1->due_date == 12345,
          "borrowed copy round-trips with holder and due date");
    CHECK(r2 && r2->state == COPY_LOST && strcmp(r2->lost_reason_code, "WATER_DAMAGE") == 0 && r2->current_holder_id[0] == '\0',
          "lost copy round-trips with an empty holder field intact");
    remove(path);

    printf(failures == 0 ? "test_copy: all checks passed\n" : "test_copy: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
