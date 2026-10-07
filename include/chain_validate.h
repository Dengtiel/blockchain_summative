#ifndef CHAIN_VALIDATE_H
#define CHAIN_VALIDATE_H

#include "persistence.h"

/* ============================================================
 * chain_validate.h
 * Full chain verification and tamper detection. Nothing stored is
 * trusted: every guarantee is recomputed from the data.
 *
 * For every block:
 *   - index is sequential and the genesis block is well formed
 *   - previous_hash equals the previous block's actual hash
 *   - recomputing the hash from the header reproduces the stored hash
 *   - the hash meets the difficulty recorded in the block
 *   - the Merkle root recomputed from the records matches merkle_root
 *   - the timestamp is strictly greater than the previous block's
 *   - token_reward equals the sum of the block's return bonuses
 * For every record:
 *   - the ECDSA signature verifies against the embedded public key
 *   - that public key is the one registered for the signer
 *   - the signer is the member (or the librarian for OVERDUE flags)
 *   - the member, book and copy exist in the registries (referential
 *     integrity) and record ids are unique
 *   - the member's nonces run 0, 1, 2, ... with no gaps or repeats
 * ============================================================ */

typedef enum {
    TAMPER_RECORD,      /* alter a lending record inside a block */
    TAMPER_BLOCK,       /* alter a block header field */
    TAMPER_LINK         /* alter a previous_hash pointer */
} TamperKind;

/* Validates the whole chain. When `report` is non-zero every failure is
 * printed on its own FAIL line followed by a summary. Returns 1 if the
 * chain is valid, 0 otherwise. */
int chain_validate(const AppState *state, int report);

/* Temporarily corrupts one block in memory, runs validation to show the
 * corruption being caught, then restores the original value so nothing
 * tampered is ever saved. Returns 0 if the demo ran, -1 if the chain has
 * nothing to tamper with. */
int chain_tamper_demo(AppState *state, TamperKind kind);

#endif
