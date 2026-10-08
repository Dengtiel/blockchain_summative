/* Unit test: pending pool ordering, status handling and persistence. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pending_pool.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

static LendingRecord rec(const char *member, LendingAction action) {
    LendingRecord r;
    memset(&r, 0, sizeof(r));
    snprintf(r.member_id, MEMBER_ID_LEN, "%s", member);
    r.action = action;
    return r;
}

int main(void) {
    static PendingPool pool;
    pool.count = 0;
    pool.next_seq = 0;

    LendingRecord normal1 = rec("M001", ACTION_BORROWED), normal2 = rec("M002", ACTION_BORROWED);
    LendingRecord res_new = rec("M003", ACTION_BORROWED), res_old = rec("M004", ACTION_BORROWED);
    LendingRecord late = rec("M005", ACTION_RETURNED);

    CHECK(pending_pool_add_lending(&pool, "R1", &normal1, 0, 0, 100) != NULL, "add a normal borrow");
    CHECK(pending_pool_add_lending(&pool, "R2", &res_new, 0, 500, 101) != NULL, "add a borrow fulfilling a newer reservation");
    CHECK(pending_pool_add_lending(&pool, "R3", &normal2, 0, 0, 102) != NULL, "add a second normal borrow");
    CHECK(pending_pool_add_lending(&pool, "R4", &res_old, 0, 200, 103) != NULL, "add a borrow fulfilling an older reservation");
    CHECK(pending_pool_add_lending(&pool, "R5", &late, 1, 0, 104) != NULL, "add an overdue-related return");
    CHECK(pending_pool_add_lending(&pool, "R1", &normal1, 0, 0, 105) == NULL, "a duplicate request_id is refused");
    CHECK(pool.count == 5 && pool.entries[0].status == POOL_PENDING, "entries start PENDING");

    pending_pool_sort(&pool);
    CHECK(strcmp(pool.entries[0].request_id, "R5") == 0, "overdue-related request is served first");
    CHECK(strcmp(pool.entries[1].request_id, "R4") == 0 && strcmp(pool.entries[2].request_id, "R2") == 0,
          "then reservations by ascending date (longest wait first)");
    CHECK(strcmp(pool.entries[3].request_id, "R1") == 0 && strcmp(pool.entries[4].request_id, "R3") == 0,
          "then the rest in arrival order");

    TokenTx tx = { "M001", "LIBRARY", 5, 0, 0, 0, 1 };
    PendingEntry *t = pending_pool_add_transfer(&pool, "T1", &tx, 1, 110);
    CHECK(t && t->kind == ENTRY_TRANSFER && strcmp(pending_entry_owner(t), "M001") == 0, "a token transfer enters the same pool");
    pending_pool_sort(&pool);
    CHECK(strcmp(pool.entries[0].request_id, "R5") == 0 && strcmp(pool.entries[1].request_id, "T1") == 0,
          "an overdue-related fine settlement ranks with the overdue tier");

    CHECK(pending_pool_count_lending(&pool, "M001") == 1 && pending_pool_count_transfers(&pool, "M001") == 1,
          "pending counts per member");

    CHECK(pending_pool_mark_suspicious(&pool, "R3", "test reason") == 0 &&
          pending_pool_find(&pool, "R3")->status == POOL_SUSPICIOUS &&
          strcmp(pending_pool_find(&pool, "R3")->fraud_reason, "test reason") == 0,
          "an entry can be flagged SUSPICIOUS with a reason");
    CHECK(pending_pool_mark_suspicious(&pool, "NOPE", "x") == -1, "flagging an unknown entry fails");
    CHECK(pending_pool_mark_confirmed(&pool, "R1") == 0 && pending_pool_count_lending(&pool, "M001") == 0,
          "a CONFIRMED entry no longer counts as pending");

    const char *path = "/tmp/lbt_pool_test.dat";
    CHECK(pending_pool_save(&pool, path) == 0, "pool saves");
    static PendingPool loaded;
    CHECK(pending_pool_load(&loaded, path) == 0 && loaded.count == pool.count && loaded.next_seq == pool.next_seq,
          "pool loads back with its sequence counter");
    CHECK(pending_pool_find(&loaded, "R3") && pending_pool_find(&loaded, "R3")->status == POOL_SUSPICIOUS,
          "statuses survive a reload");

    CHECK(pending_pool_remove(&pool, "R2") == 0 && pending_pool_find(&pool, "R2") == NULL && pool.count == 5,
          "remove deletes exactly one entry");
    CHECK(pending_pool_remove(&pool, "R2") == -1, "removing twice fails");

    FILE *f = fopen(path, "wb");
    fputs("garbage", f);
    fclose(f);
    fprintf(stderr, "-- expected warning follows --\n");
    CHECK(pending_pool_load(&loaded, path) == -1 && loaded.count == 0, "a corrupt pool file is detected and ignored");
    remove(path);

    printf(failures == 0 ? "test_pending_pool: all checks passed\n" : "test_pending_pool: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
