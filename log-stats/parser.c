#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#define FAIL                                                            \
    do {                                                                \
        fprintf(stderr, "parse_combined: FAIL at line %d\n", __LINE__); \
        return -1;                                                      \
    } while (0)
/*
189.52.44.152 - - [16/Apr/2026:13:02:01 +0000] "GET /page/10 HTTP/1.1" 200 9244
"https://google.com/" "Mozilla/5.0"
*/
int parse_combined(char* line_p, size_t len, log_entry_t* out) {
    /* пропустим IP-адрес, ident, user */
    for (int i = 0; i < 3; i++) {
        line_p = strchr(line_p, ' ');
        if (!line_p) FAIL;
        while (*line_p == ' ') line_p++;
    }

    /* пропустим дату */
    printf("remaining string %s\n", line_p);
    line_p = strchr(line_p, '[');
    if (!line_p) FAIL;
    line_p = strchr(line_p, ']');
    if (!line_p) FAIL;
    line_p++;
    while (*line_p == ' ') line_p++;

    if (*line_p != '"') FAIL;
    ++line_p;
    char* rq_end = strchr(line_p, '"');
    if (!rq_end) FAIL;

    /* внутри [line_p..rq_end) — "GET /path HTTP/1.1": URL между первым и
     * последним пробелом */
    char* url_start = strchr(line_p, ' ');
    if (!url_start || url_start >= rq_end) FAIL;
    url_start++;
    char saved = *rq_end;
    *rq_end = '\0';
    char* url_end = strrchr(line_p, ' ');
    *rq_end = saved;
    if (!url_end || url_end >= rq_end) FAIL;
    size_t url_len = url_end - url_start;
    out->url = url_start;
    out->url_len = url_len;

    /* пропустим статус */
    line_p = rq_end + 1;
    while (*line_p == ' ') line_p++;
    line_p = strchr(line_p, ' ');
    if (!line_p) FAIL;
    while (*line_p == ' ') line_p++;

    /* засунем размер в cтатистику */
    char* size_end = strchr(line_p, ' ');
    if (!size_end) FAIL;
    *size_end = '\0';
    if (strcmp(line_p, "-") == 0) {
        out->bytes_sent = 0;
    } else {
        char* endptr = NULL;
        out->bytes_sent = strtoull(line_p, &endptr, 10);
        if (*endptr != '\0') FAIL; /* не число */
    }

    /* засунем referer в статистику */
    line_p = size_end + 1;
    while (*line_p == ' ') line_p++;
    if (*line_p != '"') FAIL;
    line_p++;
    char* ref_end = strchr(line_p, '"');
    if (!ref_end) FAIL;
    size_t ref_len = ref_end - line_p;
    out->referer = line_p;
    out->referer_len = ref_len;
    return 0;
}
