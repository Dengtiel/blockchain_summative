/* Unit test: input validation helpers. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "input.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

int main(void) {
    CHECK(input_valid_id("M001", 20) && input_valid_id("B001-C2", 24) && input_valid_id("MINER_1", 20),
          "ordinary ids are accepted");
    CHECK(!input_valid_id("", 20), "empty id is rejected");
    CHECK(!input_valid_id("M 01", 20), "id with a space is rejected");
    CHECK(!input_valid_id("M|01", 20), "id containing the file separator is rejected");
    CHECK(!input_valid_id("M001;rm", 20), "id with punctuation is rejected");
    CHECK(!input_valid_id("ABCDEFGHIJKLMNOPQRST", 20), "id that does not fit its buffer is rejected");
    CHECK(!input_valid_id(NULL, 20), "NULL id is rejected");

    CHECK(input_valid_text("Alice Mugisha", 50) && input_valid_text("C Programming", 80), "ordinary text is accepted");
    CHECK(!input_valid_text("a|b", 50), "text containing '|' is rejected");
    CHECK(!input_valid_text("", 50), "empty text is rejected");
    CHECK(!input_valid_text("tab\there", 50), "control characters are rejected");

    long v = 0;
    CHECK(input_parse_int("42", 0, 100, &v) == 0 && v == 42, "valid integer parses");
    CHECK(input_parse_int("-3", -5, 5, &v) == 0 && v == -3, "negative integer within range parses");
    CHECK(input_parse_int("101", 0, 100, &v) != 0, "integer above the maximum is rejected");
    CHECK(input_parse_int("-1", 0, 100, &v) != 0, "negative amount is rejected where not allowed");
    CHECK(input_parse_int("12abc", 0, 100, &v) != 0, "trailing junk is rejected");
    CHECK(input_parse_int("", 0, 100, &v) != 0, "empty string is rejected");
    CHECK(input_parse_int("99999999999999999999", 0, 100, &v) != 0, "overflow is rejected");

    time_t d = 0;
    CHECK(input_parse_date("2026-10-17", &d) == 0, "valid date parses");
    CHECK(input_parse_date("2024-02-29", &d) == 0, "leap day in a leap year parses");
    CHECK(input_parse_date("2026-02-29", &d) != 0, "leap day in a non-leap year is rejected");
    CHECK(input_parse_date("2026-13-01", &d) != 0, "month 13 is rejected");
    CHECK(input_parse_date("2026-04-31", &d) != 0, "April 31 is rejected");
    CHECK(input_parse_date("2026/10/17", &d) != 0, "wrong separators are rejected");
    CHECK(input_parse_date("26-10-17", &d) != 0, "short year is rejected");
    CHECK(input_parse_date("2026-1a-17", &d) != 0, "non-digits are rejected");

    time_t now = 0, past = 0, future = 0;
    input_parse_date("2026-10-07", &now);
    input_parse_date("2026-10-06", &past);
    input_parse_date("2026-10-07", &future);
    CHECK(!input_date_not_in_past(past, now), "yesterday is in the past");
    CHECK(input_date_not_in_past(future, now), "today is not in the past");

    char line[] = "a||c|";
    char *f[8];
    int n = input_split_pipe(line, f, 8);
    CHECK(n == 4 && strcmp(f[0], "a") == 0 && strcmp(f[1], "") == 0 && strcmp(f[2], "c") == 0 && strcmp(f[3], "") == 0,
          "pipe split keeps empty fields");

    char nl[] = "text\r\n";
    CHECK(strcmp(input_trim_newline(nl), "text") == 0, "trailing CR/LF is trimmed");

    printf(failures == 0 ? "test_input: all checks passed\n" : "test_input: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
