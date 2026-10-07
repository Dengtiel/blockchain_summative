/* ============================================================
 * input.c
 * Input validation and small parsing helpers.
 * ============================================================ */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input.h"

int input_valid_id(const char *s, size_t max_len) {
    if (!s) return 0;
    size_t len = strlen(s);
    if (len == 0 || len >= max_len) return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '_' || c == '-')) return 0;
    }
    return 1;
}

int input_valid_text(const char *s, size_t max_len) {
    if (!s) return 0;
    size_t len = strlen(s);
    if (len == 0 || len >= max_len) return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '|' || !isprint(c)) return 0;
    }
    return 1;
}

int input_parse_int(const char *s, long min, long max, long *out) {
    if (!s || *s == '\0') return -1;
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return -1;
    if (v < min || v > max) return -1;
    *out = v;
    return 0;
}

static int days_in_month(int year, int month) {
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return days[month - 1];
}

int input_parse_date(const char *s, time_t *out) {
    if (!s || strlen(s) != 10 || s[4] != '-' || s[7] != '-') return -1;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) continue;
        if (!isdigit((unsigned char)s[i])) return -1;
    }

    int year = atoi(s), month = atoi(s + 5), day = atoi(s + 8);
    if (year < 1970 || year > 2200 || month < 1 || month > 12) return -1;
    if (day < 1 || day > days_in_month(year, month)) return -1;

    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = year - 1900;
    tmv.tm_mon = month - 1;
    tmv.tm_mday = day;
    tmv.tm_hour = 23;
    tmv.tm_min = 59;
    tmv.tm_sec = 59;
    tmv.tm_isdst = -1;

    time_t t = mktime(&tmv);
    if (t == (time_t)-1) return -1;
    *out = t;
    return 0;
}

int input_date_not_in_past(time_t d, time_t now) {
    struct tm start = *localtime(&now);
    start.tm_hour = 0;
    start.tm_min = 0;
    start.tm_sec = 0;
    start.tm_isdst = -1;
    time_t day_start = mktime(&start);
    return d >= day_start;
}

int input_split_pipe(char *line, char *fields[], int max_fields) {
    int n = 0;
    if (max_fields <= 0) return 0;
    fields[n++] = line;
    for (char *p = line; *p && n < max_fields; p++) {
        if (*p == '|') {
            *p = '\0';
            fields[n++] = p + 1;
        }
    }
    return n;
}

char *input_trim_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) s[--len] = '\0';
    return s;
}
