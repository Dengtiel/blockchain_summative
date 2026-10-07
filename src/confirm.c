/* ============================================================
 * confirm.c
 * Batch selection, re-validation, effects, sealing and mining.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "confirm.h"
#include "lending.h"
#include "mining.h"

typedef enum { APPLY_OK, APPLY_SKIP, APPLY_REJECT } ApplyResult;

typedef struct {
    AppState *state;
    LendingRecord *recs;
    size_t rec_count;
    char (*tx_ids)[HASH_HEX_LEN];
    size_t tx_count;
    int bonus;
    int fees;
    int offered;
    int transfers;
} Batch;

int confirm_pay_reward(AppState *state, const char *owner_id, int amount) {
    if (amount <= 0) return 0;
    char tx[HASH_HEX_LEN];
    if (state->meta.ledger_mode == LEDGER_MODE_UTXO)
        return utxo_credit_reward(&state->utxo_ledger, owner_id, amount, tx);
    return account_credit_reward(&state->account_ledger, owner_id, amount, tx, app_now(state));
}

static void remember_tx(Batch *b, const char *tx_id) {
    snprintf(b->tx_ids[b->tx_count++], HASH_HEX_LEN, "%s", tx_id);
}

static size_t batch_count_for(const Batch *b, const char *member_id) {
    size_t n = 0;
    for (size_t i = 0; i < b->rec_count; i++)
        if (strcmp(b->recs[i].member_id, member_id) == 0) n++;
    return n;
}

/* ---- lending entries ---- */

static ApplyResult apply_lending(Batch *b, const PendingEntry *e, char *reason, size_t rn) {
    AppState *s = b->state;
    const LendingRecord *r = &e->record;

    /* nonce: the member's chain count plus what this batch already holds */
    unsigned long expected = lending_chain_nonce(s, r->member_id) + batch_count_for(b, r->member_id);
    if (r->nonce > expected) return APPLY_SKIP;
    if (r->nonce < expected) {
        snprintf(reason, rn, "stale or reused nonce %lu (expected %lu)", r->nonce, expected);
        return APPLY_REJECT;
    }

    if (!record_verify(r)) { snprintf(reason, rn, "invalid ECDSA signature"); return APPLY_REJECT; }
    int signer_ok = strcmp(r->signer_id, r->member_id) == 0 ||
                    (strcmp(r->signer_id, LIBRARIAN_ID) == 0 && r->action == ACTION_OVERDUE);
    if (!signer_ok || !record_pubkey_matches_identity(r)) {
        snprintf(reason, rn, "signer %s is not authorised for %s", r->signer_id, r->member_id);
        return APPLY_REJECT;
    }
    if (!registry_find_member(&s->registry, r->member_id) || !registry_find_book(&s->registry, r->book_id)) {
        snprintf(reason, rn, "unknown member or book");
        return APPLY_REJECT;
    }

    Copy *c = r->copy_id[0] ? copy_find(&s->copies, r->copy_id) : NULL;
    char tx[HASH_HEX_LEN];

    switch (r->action) {
    case ACTION_BORROWED:
        if (!c || strcmp(c->book_id, r->book_id) != 0) { snprintf(reason, rn, "unknown copy %s", r->copy_id); return APPLY_REJECT; }
        if (c->state == COPY_RESERVED) {
            Reservation *front = reservation_peek_front(&s->reservations, r->book_id);
            if (!front || strcmp(front->member_id, r->member_id) != 0) {
                snprintf(reason, rn, "copy %s is reserved for another member", r->copy_id);
                return APPLY_REJECT;
            }
            if (copy_borrow(c, r->member_id, r->due_date) != 0) { snprintf(reason, rn, "copy cannot be borrowed"); return APPLY_REJECT; }
            reservation_dequeue_front(&s->reservations, r->book_id);
        } else if (copy_borrow(c, r->member_id, r->due_date) != 0) {
            snprintf(reason, rn, "copy %s is not available (%s)", r->copy_id, copy_state_to_string(c->state));
            return APPLY_REJECT;
        }
        break;

    case ACTION_RETURNED:
        if (!c || c->state != COPY_BORROWED || strcmp(c->current_holder_id, r->member_id) != 0) {
            snprintf(reason, rn, "copy %s is not borrowed by %s", r->copy_id, r->member_id);
            return APPLY_REJECT;
        }
        copy_return(c);
        if (e->has_condition) c->condition = e->new_condition;
        {
            int bonus = r->fine_amount > 0 ? RETURN_BONUS_LATE : RETURN_BONUS_ON_TIME;
            if (s->meta.ledger_mode == LEDGER_MODE_UTXO) utxo_credit_reward(&s->utxo_ledger, r->member_id, bonus, tx);
            else account_credit_reward(&s->account_ledger, r->member_id, bonus, tx, app_now(s));
            remember_tx(b, tx);
            b->bonus += bonus;
        }
        if (reservation_has_any(&s->reservations, c->book_id) && copy_reserve(c) == 0) b->offered++;
        break;

    case ACTION_RENEWED:
        if (!c || c->state != COPY_BORROWED || strcmp(c->current_holder_id, r->member_id) != 0) {
            snprintf(reason, rn, "copy %s is not borrowed by %s", r->copy_id, r->member_id);
            return APPLY_REJECT;
        }
        if (reservation_has_other_than(&s->reservations, c->book_id, r->member_id)) {
            snprintf(reason, rn, "another member has reserved %s", c->book_id);
            return APPLY_REJECT;
        }
        if (copy_renew(c, r->due_date, s->meta.max_renewals) != 0) {
            snprintf(reason, rn, "renewal limit reached or copy not borrowed");
            return APPLY_REJECT;
        }
        break;

    case ACTION_RESERVED:
        if (reservation_enqueue(&s->reservations, r->book_id, r->member_id,
                                e->reservation_date ? e->reservation_date : e->submitted_at) != 0) {
            snprintf(reason, rn, "reservation refused (duplicate or queue full)");
            return APPLY_REJECT;
        }
        break;

    case ACTION_OVERDUE:
        break;   /* a flag only: it changes no inventory or balance */
    }

    b->recs[b->rec_count++] = *r;
    return APPLY_OK;
}

/* ---- transfer entries ---- */

static ApplyResult apply_transfer(Batch *b, const PendingEntry *e, char *reason, size_t rn) {
    AppState *s = b->state;
    const TokenTx *t = &e->tx;
    char tx[HASH_HEX_LEN];

    if (t->ledger_mode != (int)s->meta.ledger_mode) return APPLY_SKIP;   /* waits for its own mode */

    if (s->meta.ledger_mode == LEDGER_MODE_ACCOUNT) {
        unsigned long next = account_next_nonce(&s->account_ledger, t->sender);
        if (t->nonce > next) return APPLY_SKIP;
        if (t->nonce < next) {
            snprintf(reason, rn, "stale or reused nonce %lu (expected %lu)", t->nonce, next);
            return APPLY_REJECT;
        }
        if (account_transfer(&s->account_ledger, t->sender, t->recipient, t->amount, t->fee, t->nonce, tx, app_now(s)) != 0) {
            snprintf(reason, rn, "transfer refused (insufficient balance)");
            return APPLY_REJECT;
        }
    } else if (utxo_transfer(&s->utxo_ledger, t->sender, t->recipient, t->amount, t->fee, tx) != 0) {
        snprintf(reason, rn, "transfer refused (insufficient unspent outputs)");
        return APPLY_REJECT;
    }

    if (t->is_fine) fines_add_settled(&s->fines, t->sender, t->amount);
    remember_tx(b, tx);
    b->fees += t->fee;
    b->transfers++;
    return APPLY_OK;
}

/* After a transfer is rejected, closes the gap in that sender's pending
 * account-model nonces so the rest can still be mined. */
static void renumber_transfers(AppState *s, const char *sender) {
    if (s->meta.ledger_mode != LEDGER_MODE_ACCOUNT) return;
    unsigned long next = account_next_nonce(&s->account_ledger, sender);
    for (;;) {
        PendingEntry *pick = NULL;
        for (size_t i = 0; i < s->pool.count; i++) {
            PendingEntry *e = &s->pool.entries[i];
            if (e->kind != ENTRY_TRANSFER || strcmp(e->tx.sender, sender) != 0) continue;
            if (e->tx.ledger_mode != (int)LEDGER_MODE_ACCOUNT || e->tx.nonce < next) continue;
            if (!pick || e->tx.nonce < pick->tx.nonce) pick = e;
        }
        if (!pick) break;
        pick->tx.nonce = next++;
    }
}

/* ---- the batch ---- */

int confirm_mine_batch(AppState *state, const char *sealer_id, int batch_size, MineResult *out) {
    memset(out, 0, sizeof(*out));
    if (batch_size < 1) batch_size = 1;
    if (batch_size > MAX_BATCH_SIZE) batch_size = MAX_BATCH_SIZE;
    if (state->locked) {
        snprintf(out->err, sizeof(out->err), "The chain failed validation: mining is disabled.");
        return -1;
    }
    if (state->chain_corrupt) {
        snprintf(out->err, sizeof(out->err), "The chain file is corrupt: mining is disabled.");
        return -1;
    }

    Batch b;
    memset(&b, 0, sizeof(b));
    b.state = state;
    b.recs = calloc((size_t)batch_size, sizeof(LendingRecord));
    b.tx_ids = calloc((size_t)batch_size, HASH_HEX_LEN);
    if (!b.recs || !b.tx_ids) {
        free(b.recs); free(b.tx_ids);
        snprintf(out->err, sizeof(out->err), "Out of memory.");
        return -1;
    }

    pending_pool_sort(&state->pool);

    unsigned char done[MAX_PENDING];     /* 0 waiting, 1 accepted, 2 rejected */
    memset(done, 0, sizeof(done));
    char accepted_ids[MAX_BATCH_SIZE][REQUEST_ID_LEN];
    int accepted = 0;
    char reject_owner[MAX_REJECTED][MEMBER_ID_LEN];
    int reject_is_transfer[MAX_REJECTED];

    int progress;
    do {
        progress = 0;
        for (size_t i = 0; i < state->pool.count && accepted < batch_size; i++) {
            PendingEntry *e = &state->pool.entries[i];
            if (done[i] || e->status != POOL_PENDING) continue;

            char reason[160] = "";
            ApplyResult res = e->kind == ENTRY_LENDING ? apply_lending(&b, e, reason, sizeof(reason))
                                                       : apply_transfer(&b, e, reason, sizeof(reason));
            if (res == APPLY_SKIP) continue;

            progress = 1;
            if (res == APPLY_OK) {
                done[i] = 1;
                snprintf(accepted_ids[accepted++], REQUEST_ID_LEN, "%s", e->request_id);
            } else {
                done[i] = 2;
                if (out->rejected_count < MAX_REJECTED) {
                    RejectedEntry *rj = &out->rejected[out->rejected_count];
                    snprintf(rj->request_id, sizeof(rj->request_id), "%s", e->request_id);
                    snprintf(rj->reason, sizeof(rj->reason), "%s", reason);
                    snprintf(reject_owner[out->rejected_count], MEMBER_ID_LEN, "%s", pending_entry_owner(e));
                    reject_is_transfer[out->rejected_count] = e->kind == ENTRY_TRANSFER;
                    out->rejected_count++;
                }
            }
        }
    } while (progress && accepted < batch_size);

    for (size_t i = 0; i < state->pool.count; i++)
        if (!done[i] && state->pool.entries[i].status == POOL_PENDING) out->deferred++;

    /* drop the rejected entries and close nonce gaps they leave behind */
    for (int k = 0; k < out->rejected_count; k++) pending_pool_remove(&state->pool, out->rejected[k].request_id);
    for (int k = 0; k < out->rejected_count; k++) {
        if (reject_is_transfer[k]) renumber_transfers(state, reject_owner[k]);
        else lending_renumber_pending(state, reject_owner[k]);
    }

    if (accepted == 0) {
        snprintf(out->err, sizeof(out->err), out->rejected_count > 0
                 ? "Nothing could be mined: every eligible request was rejected."
                 : "The pending pool has nothing eligible to mine (suspicious requests are never mined).");
        free(b.recs); free(b.tx_ids);
        return -1;
    }

    /* the header's transaction_id commits to every token transaction in the batch */
    char tx_id[HASH_HEX_LEN];
    snprintf(tx_id, sizeof(tx_id), "%s", GENESIS_HASH);
    if (b.tx_count > 0) {
        size_t len = b.tx_count * HASH_HEX_LEN + 1;
        char *joined = calloc(1, len);
        if (joined) {
            for (size_t i = 0; i < b.tx_count; i++) {
                strncat(joined, b.tx_ids[i], len - strlen(joined) - 1);
                strncat(joined, "|", len - strlen(joined) - 1);
            }
            sha256_hex((const unsigned char *)joined, strlen(joined), tx_id);
            free(joined);
        }
    }

    LendingRecord *block_recs = NULL;
    if (b.rec_count > 0) {
        block_recs = malloc(b.rec_count * sizeof(LendingRecord));
        if (block_recs) memcpy(block_recs, b.recs, b.rec_count * sizeof(LendingRecord));
    }
    Block *blk = block_create(state->chain.tail, block_recs, block_recs ? b.rec_count : 0, sealer_id,
                              b.bonus, tx_id, app_now(state));
    free(b.recs);
    free(b.tx_ids);
    if (!blk) {
        free(block_recs);
        snprintf(out->err, sizeof(out->err), "Out of memory while building the block.");
        return -1;
    }

    out->attempts = mining_solve(blk, state->meta.difficulty);
    chain_append(&state->chain, blk);

    for (int k = 0; k < accepted; k++) pending_pool_remove(&state->pool, accepted_ids[k]);

    out->confirmed = accepted;
    out->records = (int)blk->record_count;
    out->transfers = b.transfers;
    out->offered = b.offered;
    out->block_index = blk->index;
    snprintf(out->block_hash, sizeof(out->block_hash), "%s", blk->hash);
    snprintf(out->sealed_by, sizeof(out->sealed_by), "%s", blk->sealed_by);
    out->return_bonus = b.bonus;
    out->fees = b.fees;
    out->block_reward = state->meta.block_reward;
    out->total_reward = out->block_reward + out->fees;
    return 0;
}
