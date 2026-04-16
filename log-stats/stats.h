#ifndef LOG_STATS_STATS_H
#define LOG_STATS_STATS_H

#include <stddef.h>
#include <stdint.h>

typedef struct stats_table stats_table_t;

/*
 * One bucket of aggregated statistics for a specific key (URL or Referer).
 * When returned from stats_top(), `key` is a freshly allocated string — the
 * caller owns it and must free() it once done.
 */
typedef struct {
    char    *key;
    uint64_t count;
    uint64_t bytes;
} stats_entry_t;

typedef enum {
    STATS_BY_BYTES,
    STATS_BY_COUNT,
} stats_sort_t;

stats_table_t *stats_create(void);
void           stats_destroy(stats_table_t *t);

/*
 * Record one observation: +1 to count and +bytes to traffic for `key`.
 * Returns 0 on success, -1 on allocation failure.
 */
int stats_add(stats_table_t *t, const char *key, size_t key_len,
              uint64_t bytes);

/*
 * Merge all entries from src into dst. src is not modified.
 * Returns 0 on success, -1 on allocation failure.
 */
int stats_merge(stats_table_t *dst, const stats_table_t *src);

/*
 * Write up to top_n entries (sorted by `by`) into out[]. The entries'
 * `key` strings are duplicated — the caller frees them with free().
 * Returns the number actually written (<= top_n).
 */
size_t stats_top(const stats_table_t *t, stats_sort_t by,
                 stats_entry_t *out, size_t top_n);

/* Sum of `bytes` across the whole table. */
uint64_t stats_total_bytes(const stats_table_t *t);

#endif /* LOG_STATS_STATS_H */
