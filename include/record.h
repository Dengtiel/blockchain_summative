#ifndef RECORD_H
#define RECORD_H

#include <stddef.h>
#include <time.h>
#include "crypto_utils.h"

/* ============================================================
 * record.h
 * A lending record is one signed action on a physical copy
 * (borrow, return, reservation, renewal, or overdue flag).
 *
 * Every record carries:
 *   - its own per-member nonce (replay protection), and
 *   - an ECDSA signature plus the signer's public key, so the
 *     signer's identity is stored on the chain itself and any
 *     node can verify the record without a central authority.
 *
 * The signature covers every field except the signature itself.
 * ============================================================ */

#define RECORD_ID_LEN   32
#define BOOK_ID_LEN     20
#define COPY_ID_LEN     24
#define TITLE_LEN       80
#define MEMBER_ID_LEN   20
#define NAME_LEN        50

#define LIBRARIAN_ID    "LIBRARIAN"   /* signer for system-generated OVERDUE flags */

typedef enum {
    ACTION_BORROWED,
    ACTION_RETURNED,
    ACTION_RESERVED,
    ACTION_RENEWED,
    ACTION_OVERDUE
} LendingAction;

typedef struct {
    char record_id[RECORD_ID_LEN];
    char book_id[BOOK_ID_LEN];
    char copy_id[COPY_ID_LEN];          /* empty for RESERVED (no copy assigned yet) */
    char book_title[TITLE_LEN];         /* copied from the book registry at request time */
    char member_id[MEMBER_ID_LEN];
    char member_name[NAME_LEN];         /* copied from the member registry at request time */
    LendingAction action;
    time_t due_date;
    int fine_amount;                    /* 0 unless a late RETURNED or an OVERDUE record */
    unsigned long nonce;                /* per-member counter, starts at 0 */
    char signer_id[MEMBER_ID_LEN];      /* the member, or LIBRARIAN_ID */
    char signer_pubkey[PUBKEY_HEX_LEN]; /* signer's public key: the on-chain identity */
    unsigned char signature[SIG_MAX_LEN];
    unsigned int sig_len;
} LendingRecord;

const char *lending_action_to_string(LendingAction action);
int lending_action_from_string(const char *s, LendingAction *out);

/* Writes the canonical byte form of every signed field (everything
 * except the signature). This one serialization feeds the signature,
 * the record hash, and therefore the Merkle tree. */
size_t record_serialize(const LendingRecord *r, unsigned char *out, size_t out_size);

/* Stamps the signer's identity and public key onto the record and
 * signs it with that identity's private key. Returns 0 on success. */
int record_sign(LendingRecord *r, const char *signer_identity);

/* Cryptographic check only: does the signature verify against the
 * public key embedded in the record? Returns 1 if valid. */
int record_verify(const LendingRecord *r);

/* Identity check: is the embedded public key the one registered for
 * signer_id in the key store? Together with record_verify this binds
 * a record to a known identity. Returns 1 if they match. */
int record_pubkey_matches_identity(const LendingRecord *r);

/* SHA-256 of the serialized record -- a Merkle tree leaf. */
void record_hash(const LendingRecord *r, char out_hex[HASH_HEX_LEN]);

#endif
