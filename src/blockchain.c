/* ============================================================
 * blockchain.c
 * Block construction, header serialization, hash computation and
 * the linked-list chain. Proof-of-work (looping pow_nonce until the
 * hash meets the difficulty) lives in mining.c.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blockchain.h"
#include "merkle.h"

Block *block_create_genesis(time_t now) {
    Block *b = calloc(1, sizeof(Block));
    if (!b) return NULL;

    b->index = 0;
    b->timestamp = now;
    b->records = NULL;
    b->record_count = 0;
    snprintf(b->previous_hash, HASH_HEX_LEN, "%s", GENESIS_HASH);
    merkle_compute_root(NULL, 0, b->merkle_root);
    snprintf(b->sealed_by, SEALER_ID_LEN, "%s", "GENESIS");
    b->token_reward = 0;
    snprintf(b->transaction_id, TX_ID_LEN, "%s", GENESIS_HASH);
    b->pow_nonce = 0;
    b->next = NULL;

    block_compute_hash(b, b->hash);
    return b;
}

Block *block_create(const Block *previous, LendingRecord *records, size_t record_count,
                    const char *sealed_by, int token_reward, const char *transaction_id,
                    time_t now) {
    Block *b = calloc(1, sizeof(Block));
    if (!b) return NULL;

    b->index = previous->index + 1;
    /* Two blocks mined within the same second must still have strictly
     * increasing timestamps, so the new one is pushed past its parent. */
    b->timestamp = (now > previous->timestamp) ? now : previous->timestamp + 1;
    b->records = records;
    b->record_count = record_count;
    snprintf(b->previous_hash, HASH_HEX_LEN, "%s", previous->hash);
    merkle_compute_root(records, record_count, b->merkle_root);
    snprintf(b->sealed_by, SEALER_ID_LEN, "%s", sealed_by);
    b->token_reward = token_reward;
    snprintf(b->transaction_id, TX_ID_LEN, "%s", transaction_id);
    b->pow_nonce = 0;
    b->next = NULL;
    return b;
}

size_t block_header_serialize(const Block *b, unsigned char *out, size_t out_size) {
    int written = snprintf((char *)out, out_size,
        "%lu|%ld|%zu|%s|%s|%s|%d|%s|%lu|%d",
        b->index, (long)b->timestamp, b->record_count,
        b->previous_hash, b->merkle_root, b->sealed_by,
        b->token_reward, b->transaction_id, b->pow_nonce, b->difficulty);

    if (written < 0) return 0;
    return (size_t)written < out_size ? (size_t)written : out_size - 1;
}

void block_compute_hash(const Block *b, char out_hex[HASH_HEX_LEN]) {
    unsigned char buf[512];
    size_t len = block_header_serialize(b, buf, sizeof(buf));
    sha256_hex(buf, len, out_hex);
}

void block_free(Block *b) {
    if (!b) return;
    free(b->records);
    free(b);
}

/* ---- chain (singly linked list from genesis to tip) ---- */

void chain_init(Chain *chain) {
    chain->head = NULL;
    chain->tail = NULL;
    chain->length = 0;
}

void chain_append(Chain *chain, Block *new_block) {
    new_block->next = NULL;
    if (chain->tail == NULL) {
        chain->head = new_block;
    } else {
        chain->tail->next = new_block;
    }
    chain->tail = new_block;
    chain->length++;
}

void chain_free(Chain *chain) {
    Block *b = chain->head;
    while (b) {
        Block *next = b->next;
        block_free(b);
        b = next;
    }
    chain_init(chain);
}

Block *chain_get(const Chain *chain, unsigned long index) {
    for (Block *b = chain->head; b != NULL; b = b->next) {
        if (b->index == index) return b;
    }
    return NULL;
}
