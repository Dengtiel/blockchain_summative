#ifndef INPUT_H
#define INPUT_H

#include <stddef.h>
#include <time.h>

/* ============================================================
 * input.h
 * Input validation and small parsing helpers shared by the CLI
 * and the file loaders. Every user-supplied identifier, text,
 * number and date passes through one of these before it reaches
 * the ledger.
 * ============================================================ */

/* Identifiers (member, book, copy, miner ids): 1..max_len-1 characters
 * from [A-Za-z0-9_-]. The '|' separator and whitespace are rejected so
 * an id can never corrupt a pipe-delimited file. */
int input_valid_id(const char *s, size_t max_len);

/* Free text (names, titles, authors): non-empty, shorter than max_len,
 * printable, and free of '|'. */
int input_valid_text(const char *s, size_t max_len);

/* Strict base-10 integer within [min, max]. Rejects empty strings,
 * trailing junk and overflow. Returns 0 and sets *out on success. */
int input_parse_int(const char *s, long min, long max, long *out);

/* Strict YYYY-MM-DD calendar date (leap years honoured). The result is
 * 23:59:59 local time on that day -- the end of the due date. */
int input_parse_date(const char *s, time_t *out);

/* True if the end of day `d` is not before the start of `now`'s day. */
int input_date_not_in_past(time_t d, time_t now);

/* Splits a line on '|' in place, keeping empty fields (unlike sscanf's
 * %[^|]). Returns the number of fields found, up to max_fields. */
int input_split_pipe(char *line, char *fields[], int max_fields);

/* Removes a trailing newline / carriage return in place. */
char *input_trim_newline(char *s);

#endif
