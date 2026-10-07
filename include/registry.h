#ifndef REGISTRY_H
#define REGISTRY_H

#include <stddef.h>
#include "record.h"
#include "copy.h"

/* ============================================================
 * registry.h
 * Title-level Book catalog and Member directory loaded from
 * books.txt and members.txt (pipe-delimited), plus the startup
 * checks the assignment requires: every registry file must exist
 * and be non-empty, every line must be well formed, and every copy
 * in copies.txt must belong to a registered book.
 *
 *   books.txt    book_id|title|author
 *   members.txt  member_id|full_name|course_code
 *   copies.txt   (see copy.h; one physical copy per line)
 * ============================================================ */

#define MAX_BOOKS        200
#define MAX_MEMBERS      200
#define AUTHOR_LEN       50
#define COURSE_CODE_LEN  20

typedef struct {
    char book_id[BOOK_ID_LEN];
    char title[TITLE_LEN];
    char author[AUTHOR_LEN];
} Book;

typedef struct {
    char member_id[MEMBER_ID_LEN];
    char full_name[NAME_LEN];
    char course_code[COURSE_CODE_LEN];
} Member;

typedef struct {
    Book books[MAX_BOOKS];
    size_t book_count;
    Member members[MAX_MEMBERS];
    size_t member_count;
} Registry;

/* Checks that all three registry files exist and are not empty,
 * printing one error line per problem. Returns the number of problems
 * (0 means the files may be loaded). */
int registry_check_files(const char *books_path, const char *members_path, const char *copies_path);

/* Loads books and members. Malformed or duplicate lines are reported on
 * stderr with their line number and skipped. Returns 0 on success and
 * -1 if a file cannot be opened. */
int registry_load(Registry *reg, const char *books_path, const char *members_path);

int registry_save(const Registry *reg, const char *books_path, const char *members_path);

int registry_add_book(Registry *reg, const char *book_id, const char *title, const char *author);
int registry_add_member(Registry *reg, const char *member_id, const char *full_name,
                        const char *course_code);

Book *registry_find_book(Registry *reg, const char *book_id);
Member *registry_find_member(Registry *reg, const char *member_id);

/* Referential integrity between registries: every copy must reference a
 * registered book. Prints each violation and returns how many it found. */
int registry_check_copies(Registry *reg, const CopyList *copies);

#endif
