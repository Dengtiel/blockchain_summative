/* Unit test: settled-fine book. */
#include <stdio.h>
#include <stdlib.h>
#include "fines.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

int main(void) {
    static FineBook b;
    b.count = 0;
    CHECK(fines_settled(&b, "M001") == 0, "nothing is settled initially");
    CHECK(fines_add_settled(&b, "M001", 3) == 0 && fines_settled(&b, "M001") == 3, "a settlement is recorded");
    CHECK(fines_add_settled(&b, "M001", 2) == 0 && fines_settled(&b, "M001") == 5, "settlements accumulate");
    CHECK(fines_add_settled(&b, "M002", 7) == 0 && fines_settled(&b, "M002") == 7 && fines_settled(&b, "M001") == 5,
          "members are tracked separately");
    CHECK(fines_add_settled(&b, "M001", 0) == -1 && fines_add_settled(&b, "M001", -4) == -1, "non-positive amounts are refused");

    const char *path = "/tmp/lbt_fines_test.txt";
    CHECK(fines_save(&b, path) == 0, "fine book saves");
    static FineBook loaded;
    CHECK(fines_load(&loaded, path) == 0 && fines_settled(&loaded, "M001") == 5 && fines_settled(&loaded, "M002") == 7,
          "fine book loads back");
    remove(path);

    printf(failures == 0 ? "test_fines: all checks passed\n" : "test_fines: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
