/* Unit test: lending record signing, verification and tamper detection. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "record.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

static LendingRecord make_record(void) {
    LendingRecord r;
    memset(&r, 0, sizeof(r));
    snprintf(r.record_id, RECORD_ID_LEN, "REQ000001");
    snprintf(r.book_id, BOOK_ID_LEN, "B001");
    snprintf(r.copy_id, COPY_ID_LEN, "B001-C1");
    snprintf(r.book_title, TITLE_LEN, "Introduction to Algorithms");
    snprintf(r.member_id, MEMBER_ID_LEN, "M001");
    snprintf(r.member_name, NAME_LEN, "Alice Mugisha");
    r.action = ACTION_BORROWED;
    r.due_date = 1800000000;
    r.fine_amount = 0;
    r.nonce = 0;
    return r;
}

int main(void) {
    if (system("rm -rf /tmp/lbt_record_test") != 0) { /* nothing to clean */ }
    crypto_init("/tmp/lbt_record_test");

    LendingRecord r = make_record();
    CHECK(record_sign(&r, "M001") == 0 && r.sig_len > 0, "record_sign signs a record");
    CHECK(strcmp(r.signer_id, "M001") == 0 && strlen(r.signer_pubkey) == 130,
          "signer id and public key are stamped on the record");
    CHECK(record_verify(&r) == 1, "valid record verifies");
    CHECK(record_pubkey_matches_identity(&r) == 1, "embedded key matches the registered identity");

    LendingRecord t = r;
    t.fine_amount = 99;
    CHECK(record_verify(&t) == 0, "changing fine_amount breaks the signature");

    t = r; t.nonce = 7;
    CHECK(record_verify(&t) == 0, "changing the nonce breaks the signature");

    t = r; snprintf(t.member_id, MEMBER_ID_LEN, "M002");
    CHECK(record_verify(&t) == 0, "changing member_id breaks the signature");

    t = r; t.action = ACTION_RETURNED;
    CHECK(record_verify(&t) == 0, "changing the action breaks the signature");

    t = r; t.sig_len = 0;
    CHECK(record_verify(&t) == 0, "record with a missing signature is rejected");

    /* A forger signs with their own key but claims another identity's key. */
    t = r;
    record_sign(&t, "M002");
    snprintf(t.signer_id, MEMBER_ID_LEN, "M001");
    CHECK(record_verify(&t) == 0 || record_pubkey_matches_identity(&t) == 0,
          "forged signer identity is detected");

    LendingRecord lib = make_record();
    lib.action = ACTION_OVERDUE;
    CHECK(record_sign(&lib, LIBRARIAN_ID) == 0 && record_verify(&lib) == 1,
          "librarian-signed record verifies");

    char h1[HASH_HEX_LEN], h2[HASH_HEX_LEN];
    record_hash(&r, h1);
    record_hash(&r, h2);
    CHECK(strcmp(h1, h2) == 0, "record hash is deterministic");
    t = r; t.fine_amount = 1;
    record_hash(&t, h2);
    CHECK(strcmp(h1, h2) != 0, "any field change changes the record hash");

    LendingAction a;
    CHECK(lending_action_from_string("RENEWED", &a) == 0 && a == ACTION_RENEWED, "action parse");
    CHECK(lending_action_from_string("STOLEN", &a) != 0, "unknown action is rejected");
    CHECK(strcmp(lending_action_to_string(ACTION_OVERDUE), "OVERDUE") == 0, "action to string");

    printf(failures == 0 ? "test_record: all checks passed\n" : "test_record: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
