#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include "parser.h"

/*
 * Combined log format:
 * %h %l %u %t "%r" %>s %b "%{Referer}i" "%{User-agent}i"
 *
 * Sample:
 * 189.52.44.152 - - [16/Apr/2026:13:02:01 +0000] "GET /page/10 HTTP/1.1"
 *     200 9244 "https://google.com/" "Mozilla/5.0"
 *
 * Все хелперы продвигают курсор *p по строке и возвращают 0 / -1.
 */

/* Пропустить n полей, разделённых пробелами (host, ident, user). */
static int skip_space_fields(char **p, int n)
{
    for (int i = 0; i < n; i++) {
        char *sp = strchr(*p, ' ');
        if (!sp) return -1;
        while (*sp == ' ') sp++;
        *p = sp;
    }
    return 0;
}

/* Пропустить поле времени в квадратных скобках: [16/Apr/2026:.. +0000]. */
static int skip_bracketed_date(char **p)
{
    char *s = strchr(*p, '[');
    if (!s) return -1;
    s = strchr(s, ']');
    if (!s) return -1;
    s++;
    while (*s == ' ') s++;
    *p = s;
    return 0;
}

/*
 * Разобрать "%r" (request line) и достать URL — слово между первым и
 * последним пробелом внутри кавычек: "GET /path HTTP/1.1".
 */
static int parse_request_url(char **p, log_entry_t *out)
{
    char *cur = *p;
    if (*cur != '"') return -1;
    cur++;
    char *rq_end = strchr(cur, '"');
    if (!rq_end) return -1;

    char *url_start = strchr(cur, ' ');
    if (!url_start || url_start >= rq_end) return -1;
    url_start++;

    char saved = *rq_end;
    *rq_end = '\0';
    char *url_end = strrchr(cur, ' ');
    *rq_end = saved;
    if (!url_end || url_end >= rq_end) return -1;

    out->url     = url_start;
    out->url_len = (size_t)(url_end - url_start);
    *p = rq_end + 1;
    return 0;
}

/* Пропустить токен статуса (%>s) и разобрать размер (%b): число или '-'. */
static int parse_size(char **p, log_entry_t *out)
{
    char *cur = *p;
    while (*cur == ' ') cur++;
    cur = strchr(cur, ' ');         /* конец токена статуса */
    if (!cur) return -1;
    while (*cur == ' ') cur++;

    char *size_end = strchr(cur, ' ');
    if (!size_end) return -1;
    *size_end = '\0';
    if (strcmp(cur, "-") == 0) {
        out->bytes_sent = 0;
    } else {
        char *endptr = NULL;
        out->bytes_sent = strtoull(cur, &endptr, 10);
        if (endptr == cur || *endptr != '\0') return -1;
    }
    *p = size_end + 1;
    return 0;
}

/* Разобрать "%{Referer}i" — содержимое первой пары кавычек. */
static int parse_referer(char **p, log_entry_t *out)
{
    char *cur = *p;
    while (*cur == ' ') cur++;
    if (*cur != '"') return -1;
    cur++;
    char *ref_end = strchr(cur, '"');
    if (!ref_end) return -1;

    out->referer     = cur;
    out->referer_len = (size_t)(ref_end - cur);
    return 0;
}

int parse_combined(char *line, size_t len, log_entry_t *out)
{
    (void)len; /* line is NUL-terminated; len kept for future bounded reads */

    char *p = line;
    if (skip_space_fields(&p, 3) != 0) return -1;  /* host, ident, user */
    if (skip_bracketed_date(&p) != 0) return -1;   /* [timestamp]       */
    if (parse_request_url(&p, out) != 0) return -1; /* "%r" → URL       */
    if (parse_size(&p, out) != 0) return -1;        /* %>s %b           */
    if (parse_referer(&p, out) != 0) return -1;     /* "%{Referer}i"    */
    return 0;
}
