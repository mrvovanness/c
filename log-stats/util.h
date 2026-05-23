#ifndef LOG_STATS_UTIL_H
#define LOG_STATS_UTIL_H

#include <stdlib.h>

/*
 * free() + зануление указателя одним действием.
 * Защищает от use-after-free и double-free: повторный FREE_NULL по
 * уже освобождённому указателю — это free(NULL), т.е. no-op.
 */
#define FREE_NULL(p)    \
    do {                \
        free(p);        \
        (p) = NULL;     \
    } while (0)

#endif /* LOG_STATS_UTIL_H */
