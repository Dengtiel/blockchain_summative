#ifndef MERKLE_H
#define MERKLE_H

#include <stddef.h>
#include "record.h"

/* ============================================================
 * merkle.h
 * Merkle tree built from scratch over the SHA-256 hashes of a
 * block's lending records.
 *
 * Leaves are record_hash() values. Each parent is
 * SHA-256(left_hex || right_hex). When a level has an odd number of
 * nodes, the last node is paired with itself (the same rule Bitcoin
 * uses). An empty block has the root SHA-256 of the empty string.
 * ============================================================ */

#define MERKLE_MAX_DEPTH 16   /* supports up to 65,536 records per block */

/* Computes the Merkle root of `count` records. */
void merkle_compute_root(const LendingRecord *records, size_t count, char out_hex[HASH_HEX_LEN]);

/* Builds the inclusion proof for records[index]: the sibling hash at
 * every level from the leaf up to (but excluding) the root. Returns 0
 * and sets *proof_len on success, -1 if index is out of range. */
int merkle_proof(const LendingRecord *records, size_t count, size_t index,
                 char proof[][HASH_HEX_LEN], size_t *proof_len);

/* Recomputes a root from a leaf hash and its proof and compares it
 * with expected_root. Returns 1 if the leaf belongs to that root. */
int merkle_verify_proof(const char leaf_hex[HASH_HEX_LEN], const char proof[][HASH_HEX_LEN],
                        size_t proof_len, size_t index, const char *expected_root);

#endif
