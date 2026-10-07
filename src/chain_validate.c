/* ============================================================
 * chain_validate.c
 * Chain verification, tamper detection and the tamper demo.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chain_validate.h"
#include "merkle.h"
#include "mining.h"

static FILE *g_out;   /* destination for FAIL lines (see chain_validate) */

typedef struct {
    char member_id[MEMBER_ID_LEN];
    unsigned long expected_nonce;
} NonceTracker;

static int registry_has_member(const Registry *r, const char *id) {
    for (size_t i = 0; i < r->member_count; i++)
        if (strcmp(r->members[i].member_id, id) == 0) return 1;
    return 0;
}

static int registry_has_book(const Registry *r, const char *id) {
    for (size_t i = 0; i < r->book_count; i++)
        if (strcmp(r->books[i].book_id, id) == 0) return 1;
    return 0;
}

/* Returns 1 if copy_id exists and belongs to book_id. */
static int copy_matches_book(const CopyList *list, const char *copy_id, const char *book_id) {
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->copies[i].copy_id, copy_id) == 0) {
            return strcmp(list->copies[i].book_id, book_id) == 0;
        }
    }
    return 0;
}

static int expected_block_reward(const Block *b) {
    int total = 0;
    for (size_t i = 0; i < b->record_count; i++) {
        if (b->records[i].action == ACTION_RETURNED) {
            total += b->records[i].fine_amount > 0 ? RETURN_BONUS_LATE : RETURN_BONUS_ON_TIME;
        }
    }
    return total;
}

static void check_record(const AppState *s, const Block *b, const LendingRecord *r, int *failures,
                         NonceTracker **trackers, size_t *tracker_count, size_t *tracker_cap) {
    if (r->sig_len == 0) {
        fprintf(g_out, "INVALID: block #%lu record %s has no signature.\n", b->index, r->record_id);
        (*failures)++;
    } else if (!record_verify(r)) {
        fprintf(g_out, "INVALID: block #%lu record %s has an invalid ECDSA signature.\n", b->index, r->record_id);
        (*failures)++;
    }

    int signer_ok = strcmp(r->signer_id, r->member_id) == 0 ||
                    (strcmp(r->signer_id, LIBRARIAN_ID) == 0 && r->action == ACTION_OVERDUE);
    if (!signer_ok) {
        fprintf(g_out, "INVALID: block #%lu record %s is signed by %s, who may not sign for %s.\n",
               b->index, r->record_id, r->signer_id, r->member_id);
        (*failures)++;
    } else if (!record_pubkey_matches_identity(r)) {
        fprintf(g_out, "INVALID: block #%lu record %s carries a public key that is not %s's registered key.\n",
               b->index, r->record_id, r->signer_id);
        (*failures)++;
    }

    if (!registry_has_member(&s->registry, r->member_id)) {
        fprintf(g_out, "INVALID: block #%lu record %s references unregistered member %s.\n", b->index, r->record_id, r->member_id);
        (*failures)++;
    }
    if (!registry_has_book(&s->registry, r->book_id)) {
        fprintf(g_out, "INVALID: block #%lu record %s references unregistered book %s.\n", b->index, r->record_id, r->book_id);
        (*failures)++;
    }
    if (r->action != ACTION_RESERVED && !copy_matches_book(&s->copies, r->copy_id, r->book_id)) {
        fprintf(g_out, "INVALID: block #%lu record %s references copy %s, which is not a registered copy of %s.\n",
               b->index, r->record_id, r->copy_id, r->book_id);
        (*failures)++;
    }

    /* per-member nonce sequence */
    size_t idx = *tracker_count;
    for (size_t t = 0; t < *tracker_count; t++) {
        if (strcmp((*trackers)[t].member_id, r->member_id) == 0) { idx = t; break; }
    }
    if (idx == *tracker_count) {
        if (*tracker_count == *tracker_cap) {
            *tracker_cap = *tracker_cap ? *tracker_cap * 2 : 16;
            *trackers = realloc(*trackers, *tracker_cap * sizeof(NonceTracker));
            if (!*trackers) { (*failures)++; return; }
        }
        snprintf((*trackers)[idx].member_id, MEMBER_ID_LEN, "%s", r->member_id);
        (*trackers)[idx].expected_nonce = 0;
        (*tracker_count)++;
    }
    if (r->nonce != (*trackers)[idx].expected_nonce) {
        fprintf(g_out, "INVALID: block #%lu record %s has nonce %lu but %lu was expected for %s (replay or reordering).\n",
               b->index, r->record_id, r->nonce, (*trackers)[idx].expected_nonce, r->member_id);
        (*failures)++;
    }
    (*trackers)[idx].expected_nonce = r->nonce + 1;
}

int chain_validate(const AppState *state, int report) {
    int failures = 0;
    const Chain *chain = &state->chain;
    NonceTracker *trackers = NULL;
    size_t tracker_count = 0, tracker_cap = 0;
    const Block *prev = NULL;

    /* failures are written to g_out: the terminal for a reported check,
     * or a discard stream when the caller only wants the yes/no answer */
    FILE *sink = report ? NULL : fopen("/dev/null", "w");
    g_out = report ? stdout : (sink ? sink : stdout);

    if (chain->head == NULL) {
        fprintf(g_out, "INVALID: the chain is empty (no genesis block).\n");
        failures++;
    }

    char (*seen_ids)[RECORD_ID_LEN] = NULL;
    size_t seen_count = 0, seen_cap = 0;

    for (const Block *b = chain->head; b != NULL; b = b->next) {
        unsigned long expected_index = prev ? prev->index + 1 : 0;
        if (b->index != expected_index) {
            fprintf(g_out, "INVALID: block at position %lu has index %lu.\n", expected_index, b->index);
            failures++;
        }

        const char *expected_prev = prev ? prev->hash : GENESIS_HASH;
        if (strcmp(b->previous_hash, expected_prev) != 0) {
            fprintf(g_out, "INVALID: block #%lu previous_hash does not match the hash of block #%lu.\n",
                   b->index, prev ? prev->index : 0);
            failures++;
        }

        char recomputed[HASH_HEX_LEN];
        block_compute_hash(b, recomputed);
        if (strcmp(recomputed, b->hash) != 0) {
            fprintf(g_out, "INVALID: block #%lu stored hash does not match its recomputed hash (block was altered).\n", b->index);
            failures++;
        }

        if (b->index > 0) {
            if (b->difficulty < MIN_DIFFICULTY || !mining_hash_meets_difficulty(b->hash, b->difficulty)) {
                fprintf(g_out, "INVALID: block #%lu hash does not satisfy proof-of-work at difficulty %d.\n", b->index, b->difficulty);
                failures++;
            }
        }

        if ((b->record_count > 0 && b->records == NULL) || (b->index == 0 && b->record_count != 0)) {
            fprintf(g_out, "INVALID: block #%lu has an inconsistent record count.\n", b->index);
            failures++;
        } else {
            char merkle[HASH_HEX_LEN];
            merkle_compute_root(b->records, b->record_count, merkle);
            if (strcmp(merkle, b->merkle_root) != 0) {
                fprintf(g_out, "INVALID: block #%lu merkle_root does not match the root recomputed from its records.\n", b->index);
                failures++;
            }
        }

        if (prev && b->timestamp <= prev->timestamp) {
            fprintf(g_out, "INVALID: block #%lu timestamp is not strictly greater than block #%lu's.\n", b->index, prev->index);
            failures++;
        }

        if (b->index > 0 && b->token_reward != expected_block_reward(b)) {
            fprintf(g_out, "INVALID: block #%lu token_reward is %d but its records earn %d.\n",
                   b->index, b->token_reward, expected_block_reward(b));
            failures++;
        }

        if (b->records != NULL || b->record_count == 0) {
            for (size_t i = 0; i < b->record_count; i++) {
                const LendingRecord *r = &b->records[i];

                int dup = 0;
                for (size_t k = 0; k < seen_count; k++) {
                    if (strcmp(seen_ids[k], r->record_id) == 0) { dup = 1; break; }
                }
                if (dup) {
                    fprintf(g_out, "INVALID: block #%lu record id %s appears more than once on the chain.\n", b->index, r->record_id);
                    failures++;
                } else {
                    if (seen_count == seen_cap) {
                        seen_cap = seen_cap ? seen_cap * 2 : 64;
                        seen_ids = realloc(seen_ids, seen_cap * RECORD_ID_LEN);
                    }
                    if (seen_ids) snprintf(seen_ids[seen_count++], RECORD_ID_LEN, "%s", r->record_id);
                }

                check_record(state, b, r, &failures, &trackers, &tracker_count, &tracker_cap);
            }
        }

        prev = b;
    }

    free(trackers);
    free(seen_ids);

    if (sink) fclose(sink);
    g_out = stdout;

    if (report) {
        if (failures == 0) fprintf(g_out, "Chain is VALID (%lu block(s) checked).\n", chain->length);
        else fprintf(g_out, "Chain is INVALID: %d problem(s) found in %lu block(s).\n", failures, chain->length);
    }
    return failures == 0;
}

int chain_tamper_demo(AppState *state, TamperKind kind) {
    Block *target = NULL;

    if (kind == TAMPER_RECORD) {
        for (Block *b = state->chain.head; b != NULL; b = b->next) {
            if (b->index > 0 && b->record_count > 0) { target = b; break; }
        }
    } else {
        target = state->chain.head ? state->chain.head->next : NULL;
    }
    if (!target) return -1;

    if (kind == TAMPER_RECORD) {
        LendingRecord *r = &target->records[0];
        int original = r->fine_amount;
        printf("Tampering: changing fine_amount of record %s in block #%lu from %d to %d (in memory only)...\n",
               r->record_id, target->index, original, original + 25);
        r->fine_amount = original + 25;
        chain_validate(state, 1);
        r->fine_amount = original;
    } else if (kind == TAMPER_BLOCK) {
        char original[SEALER_ID_LEN];
        snprintf(original, sizeof(original), "%s", target->sealed_by);
        printf("Tampering: changing sealed_by of block #%lu from %s to ATTACKER (in memory only)...\n",
               target->index, original);
        snprintf(target->sealed_by, SEALER_ID_LEN, "%s", "ATTACKER");
        chain_validate(state, 1);
        snprintf(target->sealed_by, SEALER_ID_LEN, "%s", original);
    } else {
        char original[HASH_HEX_LEN];
        snprintf(original, sizeof(original), "%s", target->previous_hash);
        printf("Tampering: replacing previous_hash of block #%lu with a forged value (in memory only)...\n", target->index);
        snprintf(target->previous_hash, HASH_HEX_LEN, "%064d", 7);
        chain_validate(state, 1);
        snprintf(target->previous_hash, HASH_HEX_LEN, "%s", original);
    }

    printf("Tamper reverted: the in-memory chain is intact again and the file on disk was never touched.\n");
    return 0;
}
