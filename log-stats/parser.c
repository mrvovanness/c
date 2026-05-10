#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include "parser.h"

/*
 * Sample line:
 * 189.52.44.152 - - [16/Apr/2026:13:02:01 +0000] "GET /page/10 HTTP/1.1"
 *     200 9244 "https://google.com/" "Mozilla/5.0"
 */
int parse_combined(char *line_p, size_t len, log_entry_t *out)
{
    (void)len; /* line is NUL-terminated; len kept for future bounded reads */

    /* пропустим IP-адрес, ident, user */
    for (int i = 0; i < 3; i++) {
        line_p = strchr(line_p, ' ');
        if (!line_p) return -1;
        while (*line_p == ' ') line_p++;
    }

    /* пропустим дату [..] */
    line_p = strchr(line_p, '[');
    if (!line_p) return -1;
    line_p = strchr(line_p, ']');
    if (!line_p) return -1;
    line_p++;
    while (*line_p == ' ') line_p++;

    /* "%r" — request line */
    if (*line_p != '"') return -1;
    line_p++;
    char *rq_end = strchr(line_p, '"');
    if (!rq_end) return -1;

    /* внутри [line_p..rq_end) — "GET /path HTTP/1.1":
     * URL между первым и последним пробелом */
    char *url_start = strchr(line_p, ' ');
    if (!url_start || url_start >= rq_end) return -1;
    url_start++;
    char  saved   = *rq_end;
    *rq_end = '\0';
    char *url_end = strrchr(line_p, ' ');
    *rq_end = saved;
    if (!url_end || url_end >= rq_end) return -1;
    out->url     = url_start;
    out->url_len = (size_t)(url_end - url_start);

    /* пропустим пробел после '"' и сам токен статуса */
    line_p = rq_end + 1;
    while (*line_p == ' ') line_p++;
    line_p = strchr(line_p, ' ');
    if (!line_p) return -1;
    while (*line_p == ' ') line_p++;

    /* размер: число или '-' */
    char *size_end = strchr(line_p, ' ');
    if (!size_end) return -1;
    *size_end = '\0';
    if (strcmp(line_p, "-") == 0) {
        out->bytes_sent = 0;
    } else {
        char *endptr = NULL;
        out->bytes_sent = strtoull(line_p, &endptr, 10);
        if (endptr == line_p || *endptr != '\0') return -1;
    }

    /* "%{Referer}i" */
    line_p = size_end + 1;
    while (*line_p == ' ') line_p++;
    if (*line_p != '"') return -1;
    line_p++;
    char *ref_end = strchr(line_p, '"');
    if (!ref_end) return -1;
    out->referer     = line_p;
    out->referer_len = (size_t)(ref_end - line_p);
    return 0;
}
