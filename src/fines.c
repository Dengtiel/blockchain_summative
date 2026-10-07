/* ============================================================
 * fines.c
 * Per-member record of settled fines, persisted as text.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fines.h"
#include "input.h"

int fines_load(FineBook *book, const char *path) {
    book->count = 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[128];
    while (fgets(line, sizeof(line), f) && book->count < MAX_FINE_ACCOUNTS) {
        input_trim_newline(line);
        char *fld[2];
        if (input_split_pipe(line, fld, 2) != 2 || !input_valid_id(fld[0], MEMBER_ID_LEN)) continue;

        FineEntry *e = &book->entries[book->count++];
        snprintf(e->member_id, MEMBER_ID_LEN, "%s", fld[0]);
        e->settled = atoi(fld[1]);
    }
    fclose(f);
    return 0;
}

int fines_save(const FineBook *book, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (size_t i = 0; i < book->count; i++) {
        fprintf(f, "%s|%d\n", book->entries[i].member_id, book->entries[i].settled);
    }
    fclose(f);
    return 0;
}

int fines_settled(const FineBook *book, const char *member_id) {
    for (size_t i = 0; i < book->count; i++) {
        if (strcmp(book->entries[i].member_id, member_id) == 0) return book->entries[i].settled;
    }
    return 0;
}

int fines_add_settled(FineBook *book, const char *member_id, int amount) {
    if (amount <= 0) return -1;
    for (size_t i = 0; i < book->count; i++) {
        if (strcmp(book->entries[i].member_id, member_id) == 0) {
            book->entries[i].settled += amount;
            return 0;
        }
    }
    if (book->count >= MAX_FINE_ACCOUNTS) return -1;
    FineEntry *e = &book->entries[book->count++];
    snprintf(e->member_id, MEMBER_ID_LEN, "%s", member_id);
    e->settled = amount;
    return 0;
}
