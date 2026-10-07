/* ============================================================
 * registry.c
 * Disk-backed Book catalog and Member directory with startup
 * validation.
 * ============================================================ */

#include <stdio.h>
#include <string.h>
#include "registry.h"
#include "input.h"

/* A registry file is usable if it opens and holds at least one
 * non-blank line. */
static int file_has_content(const char *path, const char **problem) {
    FILE *f = fopen(path, "r");
    if (!f) { *problem = "not found or not readable"; return 0; }

    char line[512];
    int has_content = 0;
    while (fgets(line, sizeof(line), f)) {
        for (const char *p = line; *p; p++) {
            if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') { has_content = 1; break; }
        }
        if (has_content) break;
    }
    fclose(f);

    if (!has_content) { *problem = "is empty"; return 0; }
    return 1;
}

int registry_check_files(const char *books_path, const char *members_path, const char *copies_path) {
    const char *paths[3] = { books_path, members_path, copies_path };
    int problems = 0;
    for (int i = 0; i < 3; i++) {
        const char *why = NULL;
        if (!file_has_content(paths[i], &why)) {
            fprintf(stderr, "ERROR: registry file '%s' %s.\n", paths[i], why);
            problems++;
        }
    }
    return problems;
}

int registry_load(Registry *reg, const char *books_path, const char *members_path) {
    reg->book_count = 0;
    reg->member_count = 0;

    FILE *fb = fopen(books_path, "r");
    if (!fb) return -1;

    char line[512];
    int line_no = 0;
    while (fgets(line, sizeof(line), fb)) {
        line_no++;
        input_trim_newline(line);
        if (line[0] == '\0') continue;

        char *f[3];
        if (input_split_pipe(line, f, 3) != 3 ||
            !input_valid_id(f[0], BOOK_ID_LEN) || !input_valid_text(f[1], TITLE_LEN) ||
            !input_valid_text(f[2], AUTHOR_LEN)) {
            fprintf(stderr, "WARNING: %s line %d is malformed and was skipped.\n", books_path, line_no);
            continue;
        }
        if (registry_add_book(reg, f[0], f[1], f[2]) != 0) {
            fprintf(stderr, "WARNING: %s line %d duplicates or overflows the catalog; skipped.\n", books_path, line_no);
        }
    }
    fclose(fb);

    FILE *fm = fopen(members_path, "r");
    if (!fm) return -1;

    line_no = 0;
    while (fgets(line, sizeof(line), fm)) {
        line_no++;
        input_trim_newline(line);
        if (line[0] == '\0') continue;

        char *f[3];
        if (input_split_pipe(line, f, 3) != 3 ||
            !input_valid_id(f[0], MEMBER_ID_LEN) || !input_valid_text(f[1], NAME_LEN) ||
            !input_valid_text(f[2], COURSE_CODE_LEN)) {
            fprintf(stderr, "WARNING: %s line %d is malformed and was skipped.\n", members_path, line_no);
            continue;
        }
        if (registry_add_member(reg, f[0], f[1], f[2]) != 0) {
            fprintf(stderr, "WARNING: %s line %d duplicates or overflows the directory; skipped.\n", members_path, line_no);
        }
    }
    fclose(fm);
    return 0;
}

int registry_save(const Registry *reg, const char *books_path, const char *members_path) {
    FILE *fb = fopen(books_path, "w");
    if (!fb) return -1;
    for (size_t i = 0; i < reg->book_count; i++) {
        const Book *b = &reg->books[i];
        fprintf(fb, "%s|%s|%s\n", b->book_id, b->title, b->author);
    }
    fclose(fb);

    FILE *fm = fopen(members_path, "w");
    if (!fm) return -1;
    for (size_t i = 0; i < reg->member_count; i++) {
        const Member *m = &reg->members[i];
        fprintf(fm, "%s|%s|%s\n", m->member_id, m->full_name, m->course_code);
    }
    fclose(fm);
    return 0;
}

int registry_add_book(Registry *reg, const char *book_id, const char *title, const char *author) {
    if (reg->book_count >= MAX_BOOKS) return -1;
    if (registry_find_book(reg, book_id) != NULL) return -1;

    Book *b = &reg->books[reg->book_count++];
    snprintf(b->book_id, BOOK_ID_LEN, "%s", book_id);
    snprintf(b->title, TITLE_LEN, "%s", title);
    snprintf(b->author, AUTHOR_LEN, "%s", author);
    return 0;
}

int registry_add_member(Registry *reg, const char *member_id, const char *full_name,
                        const char *course_code) {
    if (reg->member_count >= MAX_MEMBERS) return -1;
    if (registry_find_member(reg, member_id) != NULL) return -1;

    Member *m = &reg->members[reg->member_count++];
    snprintf(m->member_id, MEMBER_ID_LEN, "%s", member_id);
    snprintf(m->full_name, NAME_LEN, "%s", full_name);
    snprintf(m->course_code, COURSE_CODE_LEN, "%s", course_code);
    return 0;
}

Book *registry_find_book(Registry *reg, const char *book_id) {
    for (size_t i = 0; i < reg->book_count; i++) {
        if (strcmp(reg->books[i].book_id, book_id) == 0) return &reg->books[i];
    }
    return NULL;
}

Member *registry_find_member(Registry *reg, const char *member_id) {
    for (size_t i = 0; i < reg->member_count; i++) {
        if (strcmp(reg->members[i].member_id, member_id) == 0) return &reg->members[i];
    }
    return NULL;
}

int registry_check_copies(Registry *reg, const CopyList *copies) {
    int violations = 0;
    for (size_t i = 0; i < copies->count; i++) {
        if (!registry_find_book(reg, copies->copies[i].book_id)) {
            fprintf(stderr, "ERROR: copy %s references unregistered book %s.\n",
                    copies->copies[i].copy_id, copies->copies[i].book_id);
            violations++;
        }
    }
    return violations;
}
