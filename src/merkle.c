/* ============================================================
 * merkle.c
 * Merkle tree construction, inclusion proofs and proof checking,
 * written from scratch on top of sha256_hex_pair().
 * ============================================================ */

#include <stdlib.h>
#include <string.h>
#include "merkle.h"

/* Collapses one level of the tree into its parent level (in place
 * would lose the sibling, so a fresh buffer is returned). */
static char *next_level(const char *level, size_t level_count, size_t *next_count) {
    size_t n = (level_count + 1) / 2;
    char *out = malloc(n * HASH_HEX_LEN);
    if (!out) return NULL;

    for (size_t i = 0; i < n; i++) {
        const char *left = level + (2 * i) * HASH_HEX_LEN;
        const char *right = (2 * i + 1 < level_count) ? level + (2 * i + 1) * HASH_HEX_LEN : left;
        sha256_hex_pair(left, right, out + i * HASH_HEX_LEN);
    }
    *next_count = n;
    return out;
}

static char *leaf_level(const LendingRecord *records, size_t count) {
    char *level = malloc(count * HASH_HEX_LEN);
    if (!level) return NULL;
    for (size_t i = 0; i < count; i++) record_hash(&records[i], level + i * HASH_HEX_LEN);
    return level;
}

void merkle_compute_root(const LendingRecord *records, size_t count, char out_hex[HASH_HEX_LEN]) {
    if (count == 0) {
        sha256_hex((const unsigned char *)"", 0, out_hex);
        return;
    }

    char *level = leaf_level(records, count);
    if (!level) { out_hex[0] = '\0'; return; }

    size_t level_count = count;
    while (level_count > 1) {
        size_t n = 0;
        char *up = next_level(level, level_count, &n);
        free(level);
        if (!up) { out_hex[0] = '\0'; return; }
        level = up;
        level_count = n;
    }

    memcpy(out_hex, level, HASH_HEX_LEN);
    free(level);
}

int merkle_proof(const LendingRecord *records, size_t count, size_t index,
                 char proof[][HASH_HEX_LEN], size_t *proof_len) {
    if (index >= count) return -1;

    char *level = leaf_level(records, count);
    if (!level) return -1;

    size_t level_count = count, pos = index, depth = 0;
    while (level_count > 1 && depth < MERKLE_MAX_DEPTH) {
        size_t sibling = (pos % 2 == 0) ? pos + 1 : pos - 1;
        if (sibling >= level_count) sibling = pos; /* odd node pairs with itself */
        memcpy(proof[depth], level + sibling * HASH_HEX_LEN, HASH_HEX_LEN);
        depth++;

        size_t n = 0;
        char *up = next_level(level, level_count, &n);
        free(level);
        if (!up) return -1;
        level = up;
        level_count = n;
        pos /= 2;
    }

    free(level);
    *proof_len = depth;
    return 0;
}

int merkle_verify_proof(const char leaf_hex[HASH_HEX_LEN], const char proof[][HASH_HEX_LEN],
                        size_t proof_len, size_t index, const char *expected_root) {
    char current[HASH_HEX_LEN], parent[HASH_HEX_LEN];
    memcpy(current, leaf_hex, HASH_HEX_LEN);

    for (size_t i = 0; i < proof_len; i++) {
        if (index % 2 == 0) sha256_hex_pair(current, proof[i], parent);
        else                sha256_hex_pair(proof[i], current, parent);
        memcpy(current, parent, HASH_HEX_LEN);
        index /= 2;
    }
    return strcmp(current, expected_root) == 0;
}
