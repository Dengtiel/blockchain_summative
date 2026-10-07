/* ============================================================
 * record.c
 * Serialization, signing, verification and hashing of a single
 * lending record.
 * ============================================================ */

#include <stdio.h>
#include <string.h>
#include "record.h"

#define RECORD_BUF_SIZE 768

static const char *ACTION_NAMES[] = {
    "BORROWED", "RETURNED", "RESERVED", "RENEWED", "OVERDUE"
};

const char *lending_action_to_string(LendingAction action) {
    if ((int)action < 0 || action > ACTION_OVERDUE) return "UNKNOWN";
    return ACTION_NAMES[action];
}

int lending_action_from_string(const char *s, LendingAction *out) {
    for (int i = 0; i <= ACTION_OVERDUE; i++) {
        if (strcmp(s, ACTION_NAMES[i]) == 0) {
            *out = (LendingAction)i;
            return 0;
        }
    }
    return -1;
}

size_t record_serialize(const LendingRecord *r, unsigned char *out, size_t out_size) {
    int written = snprintf((char *)out, out_size,
        "%s|%s|%s|%s|%s|%s|%s|%ld|%d|%lu|%s|%s",
        r->record_id, r->book_id, r->copy_id, r->book_title,
        r->member_id, r->member_name, lending_action_to_string(r->action),
        (long)r->due_date, r->fine_amount, r->nonce,
        r->signer_id, r->signer_pubkey);

    if (written < 0) return 0;
    return (size_t)written < out_size ? (size_t)written : out_size - 1;
}

int record_sign(LendingRecord *r, const char *signer_identity) {
    /* The identity and public key are part of the signed bytes, so they
     * must be set before serializing. */
    snprintf(r->signer_id, MEMBER_ID_LEN, "%s", signer_identity);
    if (key_get_public_hex(signer_identity, r->signer_pubkey) != 0) return -1;

    unsigned char buf[RECORD_BUF_SIZE];
    size_t len = record_serialize(r, buf, sizeof(buf));
    return sign_with(signer_identity, buf, len, r->signature, &r->sig_len);
}

int record_verify(const LendingRecord *r) {
    unsigned char buf[RECORD_BUF_SIZE];
    size_t len = record_serialize(r, buf, sizeof(buf));
    return verify_with_pubkey_hex(r->signer_pubkey, buf, len, r->signature, r->sig_len);
}

int record_pubkey_matches_identity(const LendingRecord *r) {
    char registered[PUBKEY_HEX_LEN];
    if (!key_exists(r->signer_id)) return 0;
    if (key_get_public_hex(r->signer_id, registered) != 0) return 0;
    return strcmp(registered, r->signer_pubkey) == 0;
}

void record_hash(const LendingRecord *r, char out_hex[HASH_HEX_LEN]) {
    unsigned char buf[RECORD_BUF_SIZE];
    size_t len = record_serialize(r, buf, sizeof(buf));
    sha256_hex(buf, len, out_hex);
}
