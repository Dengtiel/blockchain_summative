/* Unit test: Merkle root construction, odd-leaf rule, tamper detection, proofs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "merkle.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

static LendingRecord rec(int n) {
    LendingRecord r;
    memset(&r, 0, sizeof(r));
    snprintf(r.record_id, RECORD_ID_LEN, "REQ%06d", n);
    snprintf(r.book_id, BOOK_ID_LEN, "B%03d", n);
    snprintf(r.copy_id, COPY_ID_LEN, "B%03d-C1", n);
    snprintf(r.member_id, MEMBER_ID_LEN, "M001");
    r.action = ACTION_BORROWED;
    r.nonce = (unsigned long)n;
    return r;
}

int main(void) {
    LendingRecord r[5];
    for (int i = 0; i < 5; i++) r[i] = rec(i + 1);

    char root_empty[HASH_HEX_LEN], expect_empty[HASH_HEX_LEN];
    merkle_compute_root(NULL, 0, root_empty);
    sha256_hex((const unsigned char *)"", 0, expect_empty);
    CHECK(strcmp(root_empty, expect_empty) == 0, "empty block root is SHA-256 of the empty string");

    char root1[HASH_HEX_LEN], leaf[HASH_HEX_LEN];
    merkle_compute_root(r, 1, root1);
    record_hash(&r[0], leaf);
    CHECK(strcmp(root1, leaf) == 0, "single-record root equals that record's hash");

    char root2[HASH_HEX_LEN], h0[HASH_HEX_LEN], h1[HASH_HEX_LEN], manual[HASH_HEX_LEN];
    merkle_compute_root(r, 2, root2);
    record_hash(&r[0], h0); record_hash(&r[1], h1);
    sha256_hex_pair(h0, h1, manual);
    CHECK(strcmp(root2, manual) == 0, "two-record root equals SHA-256(h0||h1)");

    char root3[HASH_HEX_LEN], h2[HASH_HEX_LEN], p01[HASH_HEX_LEN], p22[HASH_HEX_LEN];
    merkle_compute_root(r, 3, root3);
    record_hash(&r[2], h2);
    sha256_hex_pair(h0, h1, p01);
    sha256_hex_pair(h2, h2, p22);
    sha256_hex_pair(p01, p22, manual);
    CHECK(strcmp(root3, manual) == 0, "odd leaf is paired with itself");

    char a[HASH_HEX_LEN], b[HASH_HEX_LEN];
    merkle_compute_root(r, 5, a);
    merkle_compute_root(r, 5, b);
    CHECK(strcmp(a, b) == 0, "root is deterministic");

    LendingRecord t[5];
    memcpy(t, r, sizeof(r));
    t[3].fine_amount = 50;
    merkle_compute_root(t, 5, b);
    CHECK(strcmp(a, b) != 0, "changing any record changes the root");

    memcpy(t, r, sizeof(r));
    LendingRecord swap = t[0]; t[0] = t[1]; t[1] = swap;
    merkle_compute_root(t, 5, b);
    CHECK(strcmp(a, b) != 0, "reordering records changes the root");

    int all_ok = 1;
    for (size_t i = 0; i < 5; i++) {
        char proof[MERKLE_MAX_DEPTH][HASH_HEX_LEN], lh[HASH_HEX_LEN];
        size_t plen = 0;
        record_hash(&r[i], lh);
        if (merkle_proof(r, 5, i, proof, &plen) != 0 || !merkle_verify_proof(lh, proof, plen, i, a)) all_ok = 0;
    }
    CHECK(all_ok, "inclusion proof verifies for every one of 5 records");

    char proof[MERKLE_MAX_DEPTH][HASH_HEX_LEN], lh[HASH_HEX_LEN];
    size_t plen = 0;
    merkle_proof(r, 5, 2, proof, &plen);
    record_hash(&t[2], lh);
    t[2].fine_amount = 7;
    record_hash(&t[2], lh);
    CHECK(!merkle_verify_proof(lh, proof, plen, 2, a), "a modified record fails its inclusion proof");
    CHECK(merkle_proof(r, 5, 9, proof, &plen) == -1, "out-of-range proof index is rejected");

    printf(failures == 0 ? "test_merkle: all checks passed\n" : "test_merkle: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
