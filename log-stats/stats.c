#define _POSIX_C_SOURCE 200809L

#include "stats.h"

#include <stdlib.h>

struct stats_table {
    int _placeholder; /* real storage added in the next step */
};

stats_table_t *stats_create(void)
{
    return calloc(1, sizeof(stats_table_t));
}

void stats_destroy(stats_table_t *t)
{
    free(t);
}

int stats_add(stats_table_t *t, const char *key, size_t key_len,
              uint64_t bytes)
{
    (void)t;
    (void)key;
    (void)key_len;
    (void)bytes;
    return 0;
}

int stats_merge(stats_table_t *dst, const stats_table_t *src)
{
    (void)dst;
    (void)src;
    return 0;
}

size_t stats_top(const stats_table_t *t, stats_sort_t by,
                 stats_entry_t *out, size_t top_n)
{
    (void)t;
    (void)by;
    (void)out;
    (void)top_n;
    return 0;
}

uint64_t stats_total_bytes(const stats_table_t *t)
{
    (void)t;
    return 0;
}
