#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include <stddef.h>

/* ============================================================
 * crypto_utils.h
 * SHA-256 hashing and per-identity ECDSA (NIST P-256) key
 * management built on OpenSSL's EVP API.
 *
 * Every identity (a member id, or "LIBRARIAN") owns one keypair.
 * The private key never leaves the key directory; the public key
 * is exported as the 65-byte uncompressed EC point in hex and is
 * what gets embedded in every signed lending record, so a record
 * on the chain carries the signer's identity with it.
 * ============================================================ */

#define HASH_HEX_LEN     65     /* 64 hex chars + NUL */
#define SIG_MAX_LEN      80     /* DER-encoded P-256 signature is at most 72 bytes */
#define PUBKEY_HEX_LEN   131    /* 65-byte uncompressed point -> 130 hex chars + NUL */

/* Sets the directory that holds <identity>_private.pem / _public.pem
 * and creates it (mode 0700) if needed. Must be called once first. */
int crypto_init(const char *keys_dir);
void crypto_cleanup(void);

/* SHA-256 of `data`, written as 64 lowercase hex characters. */
void sha256_hex(const unsigned char *data, size_t len, char out_hex[HASH_HEX_LEN]);

/* SHA-256 of the concatenation of two hex digests (Merkle inner node). */
void sha256_hex_pair(const char *left_hex, const char *right_hex, char out_hex[HASH_HEX_LEN]);

/* Generates the identity's P-256 keypair if it does not exist yet.
 * Returns 0 on success (including "already existed"), -1 on failure. */
int key_ensure(const char *identity_id);
int key_exists(const char *identity_id);

/* Writes the identity's public key (uncompressed point, hex). */
int key_get_public_hex(const char *identity_id, char out_hex[PUBKEY_HEX_LEN]);

/* ECDSA-SHA256 signature over `data` with the identity's private key.
 * Returns 0 on success. */
int sign_with(const char *identity_id, const unsigned char *data, size_t len,
              unsigned char sig_out[SIG_MAX_LEN], unsigned int *sig_len_out);

/* Verifies a signature against a public key given as hex (the key
 * embedded in a record). Returns 1 if valid, 0 otherwise. */
int verify_with_pubkey_hex(const char *pubkey_hex, const unsigned char *data, size_t len,
                           const unsigned char *sig, unsigned int sig_len);

#endif
