/* Unit test: SHA-256 vectors, ECDSA key generation, signing, verification. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "crypto_utils.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

int main(void) {
    const char *tmpl = "/tmp/lbt_crypto_test";
    if (system("rm -rf /tmp/lbt_crypto_test") != 0) { /* nothing to clean */ }
    CHECK(crypto_init(tmpl) == 0, "crypto_init creates the key directory");

    char hex[HASH_HEX_LEN];
    sha256_hex((const unsigned char *)"abc", 3, hex);
    CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0,
          "SHA-256(\"abc\") matches the published test vector");

    sha256_hex((const unsigned char *)"", 0, hex);
    CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0,
          "SHA-256 of the empty string matches the published vector");

    CHECK(key_exists("M001") == 0, "no key exists before first use");
    CHECK(key_ensure("M001") == 0, "key_ensure generates a keypair");
    CHECK(key_exists("M001") == 1, "keypair files now exist");

    char pub[PUBKEY_HEX_LEN], pub2[PUBKEY_HEX_LEN];
    CHECK(key_get_public_hex("M001", pub) == 0 && strlen(pub) == 130 && strncmp(pub, "04", 2) == 0,
          "public key is a 65-byte uncompressed EC point");
    CHECK(key_get_public_hex("M001", pub2) == 0 && strcmp(pub, pub2) == 0,
          "public key is stable across calls");

    const unsigned char msg[] = "M001|BORROWED|B001";
    unsigned char sig[SIG_MAX_LEN];
    unsigned int sig_len = 0;
    CHECK(sign_with("M001", msg, sizeof(msg) - 1, sig, &sig_len) == 0 && sig_len > 0,
          "sign_with produces a signature");
    CHECK(verify_with_pubkey_hex(pub, msg, sizeof(msg) - 1, sig, sig_len) == 1,
          "signature verifies against the embedded public key");

    const unsigned char tampered[] = "M001|BORROWED|B002";
    CHECK(verify_with_pubkey_hex(pub, tampered, sizeof(tampered) - 1, sig, sig_len) == 0,
          "altered message fails verification");

    key_ensure("M002");
    char other_pub[PUBKEY_HEX_LEN];
    key_get_public_hex("M002", other_pub);
    CHECK(strcmp(pub, other_pub) != 0, "different identities get different keys");
    CHECK(verify_with_pubkey_hex(other_pub, msg, sizeof(msg) - 1, sig, sig_len) == 0,
          "another member's public key cannot verify the signature");

    CHECK(verify_with_pubkey_hex(pub, msg, sizeof(msg) - 1, sig, 0) == 0,
          "missing signature is rejected");
    CHECK(verify_with_pubkey_hex("not-hex", msg, sizeof(msg) - 1, sig, sig_len) == 0,
          "malformed public key is rejected");

    unsigned char bad[SIG_MAX_LEN];
    memcpy(bad, sig, sig_len);
    bad[sig_len / 2] ^= 0x01;
    CHECK(verify_with_pubkey_hex(pub, msg, sizeof(msg) - 1, bad, sig_len) == 0,
          "bit-flipped signature is rejected");

    crypto_cleanup();
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpl);
    if (system(cmd) != 0) { /* best-effort cleanup */ }

    printf(failures == 0 ? "test_crypto: all checks passed\n" : "test_crypto: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
