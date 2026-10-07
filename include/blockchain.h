#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include <stddef.h>
#include <time.h>
#include "record.h"
#include "crypto_utils.h"

/* ============================================================
 * blockchain.h
 * A block seals a batch of lending records under one Merkle root,
 * is linked to its predecessor by hash, and carries its own
 * proof-of-work nonce plus the token reward it paid out.
 *
 * Header fields (all covered by the block hash):
 *   index, timestamp, record_count, previous_hash, merkle_root,
 *   sealed_by, token_reward, transaction_id, pow_nonce
 * The block hash itself is SHA-256 over that serialized header.
 * ============================================================ */

#define SEALER_ID_LEN  24
#define TX_ID_LEN      (HASH_HEX_LEN)
#define GENESIS_HASH   "0000000000000000000000000000000000000000000000000000000000000000"

typedef struct Block {
    unsigned long index;                    /* 0 = genesis */
    time_t timestamp;                       /* strictly greater than the previous block's */
    LendingRecord *records;                 /* owned by the block */
    size_t record_count;
    char previous_hash[HASH_HEX_LEN];
    char merkle_root[HASH_HEX_LEN];
    char hash[HASH_HEX_LEN];
    char sealed_by[SEALER_ID_LEN];          /* miner or librarian node that mined the block */
    int token_reward;                       /* coins paid for this block's returns: 10 on time, 5 late */
    char transaction_id[TX_ID_LEN];         /* SHA-256 of the reward transaction(s); zeros if none */
    unsigned long pow_nonce;
    struct Block *next;
} Block;

typedef struct {
    Block *head;            /* genesis */
    Block *tail;            /* most recent block */
    unsigned long length;
} Chain;

/* Genesis block: index 0, no records, previous_hash of zeros. */
Block *block_create_genesis(time_t now);

/* Builds an unmined block on top of `previous`. The block takes
 * ownership of `records`. The timestamp is `now`, bumped to
 * previous->timestamp + 1 if needed so timestamps strictly increase.
 * The Merkle root is computed here; hash and pow_nonce are set by mining. */
Block *block_create(const Block *previous, LendingRecord *records, size_t record_count,
                    const char *sealed_by, int token_reward, const char *transaction_id,
                    time_t now);

size_t block_header_serialize(const Block *b, unsigned char *out, size_t out_size);

/* Recomputes the block hash from its current header and pow_nonce. */
void block_compute_hash(const Block *b, char out_hex[HASH_HEX_LEN]);

void block_free(Block *b);

void chain_init(Chain *chain);
void chain_append(Chain *chain, Block *new_block);
void chain_free(Chain *chain);
Block *chain_get(const Chain *chain, unsigned long index);

#endif
