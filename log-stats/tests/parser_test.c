#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "../parser.h"

int main(void)
{
    log_entry_t e = {0};

    /* Нормальная строка */
    char s1[] =
        "1.2.3.4 - - [16/Apr/2026:10:00:00 +0000] "
        "\"GET /foo HTTP/1.1\" 200 1234 \"https://ref/\" \"UA\"";
    assert(parse_combined(s1, strlen(s1), &e) == 0);
    assert(e.url_len == 4 && memcmp(e.url, "/foo", 4) == 0);
    assert(e.bytes_sent == 1234);
    assert(e.referer_len == 12 && memcmp(e.referer, "https://ref/", 12) == 0);

    /* 304 с '-' в поле размера, '-' в Referer */
    char s2[] = "1.2.3.4 - - [...] \"GET /x HTTP/1.1\" 304 - \"-\" \"UA\"";
    assert(parse_combined(s2, strlen(s2), &e) == 0);
    assert(e.url_len == 2 && memcmp(e.url, "/x", 2) == 0);
    assert(e.bytes_sent == 0);
    assert(e.referer_len == 1 && e.referer[0] == '-');

    /* Битая строка */
    char s3[] = "totally not a log line";
    assert(parse_combined(s3, strlen(s3), &e) == -1);

    /* Пустая строка */
    char s4[] = "";
    assert(parse_combined(s4, strlen(s4), &e) == -1);

    printf("parser_test: all %d cases passed\n", 4);
    return 0;
}
