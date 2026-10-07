/* Unit test: registry files, startup checks and referential integrity. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "registry.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

int main(void) {
    const char *books = "/tmp/lbt_books_test.txt", *members = "/tmp/lbt_members_test.txt",
               *copies = "/tmp/lbt_copies_reg_test.txt";
    remove(books); remove(members); remove(copies);

    fprintf(stderr, "-- expected error output follows --\n");
    CHECK(registry_check_files(books, members, copies) == 3, "three missing files are three problems");

    write_file(books, ""); write_file(members, "\n  \n"); write_file(copies, "B001-C1|B001|NEW|AVAILABLE||0|0|\n");
    CHECK(registry_check_files(books, members, copies) == 2, "empty and whitespace-only files are rejected");
    fprintf(stderr, "-- end of expected error output --\n");

    write_file(books, "B001|Introduction to Algorithms|Cormen\nB002|The C Programming Language|Kernighan\n"
                      "BAD LINE\nB001|Duplicate|Someone\nB|3|bad|extra|fields\n");
    write_file(members, "M001|Alice Mugisha|BSE\nM002|Bob Okello|BIT\nM 1|Bad Id|BSE\n");
    CHECK(registry_check_files(books, members, copies) == 0, "valid non-empty files pass the startup check");

    static Registry reg;
    fprintf(stderr, "-- expected warnings follow --\n");
    CHECK(registry_load(&reg, books, members) == 0, "registries load");
    fprintf(stderr, "-- end of expected warnings --\n");
    CHECK(reg.book_count == 2, "malformed and duplicate book lines are skipped");
    CHECK(reg.member_count == 2, "malformed member lines are skipped");

    Book *b = registry_find_book(&reg, "B002");
    CHECK(b && strcmp(b->title, "The C Programming Language") == 0 && strcmp(b->author, "Kernighan") == 0,
          "titles with spaces load intact");
    Member *m = registry_find_member(&reg, "M001");
    CHECK(m && strcmp(m->full_name, "Alice Mugisha") == 0 && strcmp(m->course_code, "BSE") == 0, "member fields load");
    CHECK(registry_find_book(&reg, "NOPE") == NULL && registry_find_member(&reg, "NOPE") == NULL, "unknown ids are not found");

    CHECK(registry_add_member(&reg, "M003", "Cara Nkusi", "BBA") == 0, "a member can be registered at runtime");
    CHECK(registry_add_member(&reg, "M003", "Dup", "X") == -1, "duplicate member ids are refused");
    CHECK(registry_add_book(&reg, "B003", "Operating Systems", "Tanenbaum") == 0, "a book can be registered at runtime");

    CHECK(registry_save(&reg, books, members) == 0, "registries save");
    static Registry again;
    registry_load(&again, books, members);
    CHECK(again.book_count == 3 && again.member_count == 3, "saved registries load back with the new entries");

    static CopyList cl;
    cl.count = 0;
    copy_add(&cl, "B001", COND_NEW);
    copy_add(&cl, "B009", COND_GOOD);
    fprintf(stderr, "-- expected integrity error follows --\n");
    CHECK(registry_check_copies(&reg, &cl) == 1, "a copy of an unregistered book violates referential integrity");

    remove(books); remove(members); remove(copies);
    printf(failures == 0 ? "test_registry: all checks passed\n" : "test_registry: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
