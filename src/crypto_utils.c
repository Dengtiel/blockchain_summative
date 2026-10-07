/* ============================================================
 * crypto_utils.c
 * SHA-256 hashing and per-identity ECDSA (P-256) key management,
 * implemented with OpenSSL 3 EVP APIs.
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/param_build.h>
#include <openssl/core_names.h>
#include "crypto_utils.h"

#define EC_POINT_LEN 65          /* 0x04 || X(32) || Y(32) */
#define KEYS_PATH_MAX 480

static char g_keys_dir[KEYS_PATH_MAX] = "keys";

/* ---- hex helpers ---- */

static void hex_encode(const unsigned char *bytes, size_t len, char *out_hex) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out_hex[i * 2]     = digits[(bytes[i] >> 4) & 0xF];
        out_hex[i * 2 + 1] = digits[bytes[i] & 0xF];
    }
    out_hex[len * 2] = '\0';
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decodes exactly `expected_len` bytes; returns -1 on any malformed input. */
static int hex_decode(const char *hex, unsigned char *out, size_t expected_len) {
    if (strlen(hex) != expected_len * 2) return -1;
    for (size_t i = 0; i < expected_len; i++) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return 0;
}

/* ---- setup ---- */

int crypto_init(const char *keys_dir) {
    snprintf(g_keys_dir, sizeof(g_keys_dir), "%s", keys_dir);

    struct stat st;
    if (stat(g_keys_dir, &st) != 0) {
        if (mkdir(g_keys_dir, 0700) != 0) {
            fprintf(stderr, "crypto_init: could not create key directory '%s'.\n", g_keys_dir);
            return -1;
        }
    }
    return 0;
}

void crypto_cleanup(void) {
    /* OpenSSL 3.x manages its own global state; nothing to release. */
}

/* ---- hashing ---- */

void sha256_hex(const unsigned char *data, size_t len, char out_hex[HASH_HEX_LEN]) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);

    hex_encode(digest, digest_len, out_hex);
}

void sha256_hex_pair(const char *left_hex, const char *right_hex, char out_hex[HASH_HEX_LEN]) {
    char buf[2 * (HASH_HEX_LEN - 1) + 1];
    snprintf(buf, sizeof(buf), "%s%s", left_hex, right_hex);
    sha256_hex((const unsigned char *)buf, strlen(buf), out_hex);
}

/* ---- key files ---- */

static void private_key_path(const char *identity_id, char *out, size_t out_size) {
    snprintf(out, out_size, "%s/%s_private.pem", g_keys_dir, identity_id);
}

static void public_key_path(const char *identity_id, char *out, size_t out_size) {
    snprintf(out, out_size, "%s/%s_public.pem", g_keys_dir, identity_id);
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int key_exists(const char *identity_id) {
    char priv_path[KEYS_PATH_MAX + 64], pub_path[KEYS_PATH_MAX + 64];
    private_key_path(identity_id, priv_path, sizeof(priv_path));
    public_key_path(identity_id, pub_path, sizeof(pub_path));
    return file_exists(priv_path) && file_exists(pub_path);
}

int key_ensure(const char *identity_id) {
    char priv_path[KEYS_PATH_MAX + 64], pub_path[KEYS_PATH_MAX + 64];
    private_key_path(identity_id, priv_path, sizeof(priv_path));
    public_key_path(identity_id, pub_path, sizeof(pub_path));

    if (file_exists(priv_path) && file_exists(pub_path)) return 0;

    EVP_PKEY *pkey = EVP_EC_gen("P-256");
    if (!pkey) {
        fprintf(stderr, "key_ensure: failed to generate a keypair for '%s'.\n", identity_id);
        return -1;
    }

    FILE *fpriv = fopen(priv_path, "w");
    FILE *fpub  = fopen(pub_path, "w");
    int ok = 1;

    if (!fpriv || !fpub) {
        fprintf(stderr, "key_ensure: could not open key files for '%s'.\n", identity_id);
        ok = 0;
    } else {
        if (!PEM_write_PrivateKey(fpriv, pkey, NULL, NULL, 0, NULL, NULL)) ok = 0;
        if (!PEM_write_PUBKEY(fpub, pkey)) ok = 0;
    }

    if (fpriv) fclose(fpriv);
    if (fpub) fclose(fpub);
    EVP_PKEY_free(pkey);

    if (!ok) {
        fprintf(stderr, "key_ensure: failed to write the keypair for '%s'.\n", identity_id);
        return -1;
    }

    chmod(priv_path, 0600); /* the private key is readable by its owner only */
    return 0;
}

static EVP_PKEY *load_private_key(const char *identity_id) {
    if (key_ensure(identity_id) != 0) return NULL;

    char path[KEYS_PATH_MAX + 64];
    private_key_path(identity_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    EVP_PKEY *pkey = PEM_read_PrivateKey(f, NULL, NULL, NULL);
    fclose(f);
    return pkey;
}

static EVP_PKEY *load_public_key_file(const char *identity_id) {
    if (key_ensure(identity_id) != 0) return NULL;

    char path[KEYS_PATH_MAX + 64];
    public_key_path(identity_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    EVP_PKEY *pkey = PEM_read_PUBKEY(f, NULL, NULL, NULL);
    fclose(f);
    return pkey;
}

int key_get_public_hex(const char *identity_id, char out_hex[PUBKEY_HEX_LEN]) {
    EVP_PKEY *pkey = load_public_key_file(identity_id);
    if (!pkey) return -1;

    /* Read the point's X and Y coordinates directly and assemble the
     * uncompressed form 0x04 || X || Y. This does not depend on the
     * point-conversion format of the loaded key, so it behaves the same
     * on every OpenSSL 3.0.x release. */
    BIGNUM *x = NULL, *y = NULL;
    unsigned char point[EC_POINT_LEN];
    int ok = EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_EC_PUB_X, &x) == 1
             && EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_EC_PUB_Y, &y) == 1
             && BN_bn2binpad(x, point + 1, 32) == 32
             && BN_bn2binpad(y, point + 33, 32) == 32;
    BN_free(x);
    BN_free(y);
    EVP_PKEY_free(pkey);
    if (!ok) {
        fprintf(stderr, "key_get_public_hex: could not read the public key of '%s'.\n", identity_id);
        return -1;
    }

    point[0] = 0x04;
    hex_encode(point, EC_POINT_LEN, out_hex);
    return 0;
}

/* Rebuilds a P-256 public key object from its hex-encoded EC point. */
static EVP_PKEY *public_key_from_hex(const char *pubkey_hex) {
    unsigned char point[EC_POINT_LEN];
    if (hex_decode(pubkey_hex, point, EC_POINT_LEN) != 0) return NULL;

    EVP_PKEY *pkey = NULL;
    OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    OSSL_PARAM *params = NULL;

    if (bld && ctx
        && OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0)
        && OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, point, EC_POINT_LEN)
        && (params = OSSL_PARAM_BLD_to_param(bld)) != NULL
        && EVP_PKEY_fromdata_init(ctx) == 1) {
        if (EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) != 1) pkey = NULL;
    }

    OSSL_PARAM_free(params);
    OSSL_PARAM_BLD_free(bld);
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

/* ---- signing / verification ---- */

int sign_with(const char *identity_id, const unsigned char *data, size_t len,
              unsigned char sig_out[SIG_MAX_LEN], unsigned int *sig_len_out) {
    EVP_PKEY *pkey = load_private_key(identity_id);
    if (!pkey) return -1;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    size_t sig_len = SIG_MAX_LEN;
    int ok = 1;

    if (EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, pkey) != 1) ok = 0;
    if (ok && EVP_DigestSign(ctx, sig_out, &sig_len, data, len) != 1) ok = 0;

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    if (!ok) return -1;
    *sig_len_out = (unsigned int)sig_len;
    return 0;
}

int verify_with_pubkey_hex(const char *pubkey_hex, const unsigned char *data, size_t len,
                           const unsigned char *sig, unsigned int sig_len) {
    if (sig_len == 0) return 0; /* a missing signature never verifies */

    EVP_PKEY *pkey = public_key_from_hex(pubkey_hex);
    if (!pkey) return 0;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    int result = 0;
    if (EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pkey) == 1) {
        result = EVP_DigestVerify(ctx, sig, sig_len, data, len) == 1;
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return result;
}
