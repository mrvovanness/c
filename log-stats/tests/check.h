#ifndef LOG_STATS_TEST_CHECK_H
#define LOG_STATS_TEST_CHECK_H

#include <stdio.h>
#include <stdlib.h>

/*
 * Проверка для тестов, НЕ зависящая от NDEBUG.
 *
 * assert() превращается в no-op, если собрать с -DNDEBUG (или с
 * оптимизацией, где NDEBUG выставлен) — тогда тест «проходит» молча,
 * ничего не проверив. CHECK работает всегда: при провале печатает
 * место и аварийно завершает процесс ненулевым кодом.
 */
#define CHECK(cond)                                          \
    do {                                                     \
        if (!(cond)) {                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n",     \
                    __FILE__, __LINE__, #cond);              \
            abort();                                         \
        }                                                    \
    } while (0)

#endif /* LOG_STATS_TEST_CHECK_H */
