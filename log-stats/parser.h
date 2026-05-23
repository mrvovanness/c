#ifndef LOG_STATS_PARSER_H
#define LOG_STATS_PARSER_H

#include <stddef.h>
#include <stdint.h>

/*
 * Result of parsing one line of the Apache "combined" access log format.
 * The `url` and `referer` pointers reference memory inside the caller's
 * line buffer — they are valid only while the buffer lives and is not
 * modified further.
 */
typedef struct {
    const char *url;
    size_t      url_len;
    const char *referer;
    size_t      referer_len;
    uint64_t    bytes_sent;      /* 0 if the size field was '-' */
} log_entry_t;

/*
 * Parse a single "combined"-format line (newline already stripped).
 * The buffer is mutated in place — NUL terminators are inserted at field
 * boundaries, so callers must not rely on buf's original contents after
 * the call.
 *
 * Returns 0 on success, -1 if the line does not match the expected format.
 */
int parse_combined(char *buf, size_t len, log_entry_t *out);

#endif /* LOG_STATS_PARSER_H */
