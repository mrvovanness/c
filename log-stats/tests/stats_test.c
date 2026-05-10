#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../stats.h"

int main(void)
{
    stats_table_t *t = stats_create();
    assert(t != NULL);

    /* пустая таблица */
    assert(stats_total_bytes(t) == 0);

    /* первая вставка */
    assert(stats_add(t, "/a", 2, 100) == 0);
    assert(stats_total_bytes(t) == 100);

    /* повторение того же ключа — count++, bytes += */
    assert(stats_add(t, "/a", 2, 50) == 0);
    assert(stats_total_bytes(t) == 150);

    /* другой ключ */
    assert(stats_add(t, "/b", 2, 200) == 0);
    assert(stats_total_bytes(t) == 350);

    /* много разных ключей — провоцируем rehash */
    char buf[32];
    for (int i = 0; i < 100; ++i) {
        int n = snprintf(buf, sizeof(buf), "/url-%d", i);
        assert(stats_add(t, buf, (size_t)n, (uint64_t)(i + 1)) == 0);
    }
    /* сумма 1..100 = 5050, плюс уже было 350 */
    assert(stats_total_bytes(t) == 350 + 5050);

    /* повтор тех же 100 ключей */
    for (int i = 0; i < 100; ++i) {
        int n = snprintf(buf, sizeof(buf), "/url-%d", i);
        assert(stats_add(t, buf, (size_t)n, 1) == 0);
    }
    assert(stats_total_bytes(t) == 350 + 5050 + 100);

    stats_destroy(t);

    /* --- merge: overlapping keys + uniques --- */
    {
        stats_table_t *a = stats_create();
        stats_table_t *b = stats_create();
        assert(a && b);

        /* a: /x = (count=2, bytes=300), /y = (count=1, bytes=10) */
        assert(stats_add(a, "/x", 2, 100) == 0);
        assert(stats_add(a, "/x", 2, 200) == 0);
        assert(stats_add(a, "/y", 2, 10) == 0);

        /* b: /x = (count=1, bytes=50), /z = (count=3, bytes=900) */
        assert(stats_add(b, "/x", 2, 50) == 0);
        assert(stats_add(b, "/z", 2, 300) == 0);
        assert(stats_add(b, "/z", 2, 300) == 0);
        assert(stats_add(b, "/z", 2, 300) == 0);

        assert(stats_merge(a, b) == 0);

        /* После merge:  /x: count=3, bytes=350; /y: 1,10; /z: 3,900 */
        assert(stats_total_bytes(a) == 350 + 10 + 900);

        stats_destroy(a);
        stats_destroy(b);  /* b не модифицируется и должен жить */
    }

    /* --- top by bytes --- */
    {
        stats_table_t *t2 = stats_create();
        assert(t2);
        assert(stats_add(t2, "/big",    4, 1000) == 0);
        assert(stats_add(t2, "/medium", 7, 500)  == 0);
        assert(stats_add(t2, "/small",  6, 10)   == 0);
        /* /tiny: count=5, bytes=5  — большой count, маленький bytes */
        for (int i = 0; i < 5; ++i)
            assert(stats_add(t2, "/tiny", 5, 1) == 0);

        stats_entry_t top[10] = {0};
        size_t n = stats_top(t2, STATS_BY_BYTES, top, 10);
        assert(n == 4);
        assert(strcmp(top[0].key, "/big")    == 0 && top[0].bytes == 1000);
        assert(strcmp(top[1].key, "/medium") == 0 && top[1].bytes == 500);
        assert(strcmp(top[2].key, "/small")  == 0 && top[2].bytes == 10);
        assert(strcmp(top[3].key, "/tiny")   == 0 && top[3].bytes == 5);
        for (size_t i = 0; i < n; ++i) free(top[i].key);

        /* по count — порядок другой: tiny впереди */
        memset(top, 0, sizeof(top));
        n = stats_top(t2, STATS_BY_COUNT, top, 10);
        assert(n == 4);
        assert(strcmp(top[0].key, "/tiny") == 0 && top[0].count == 5);
        for (size_t i = 0; i < n; ++i) free(top[i].key);

        /* top_n меньше размера */
        memset(top, 0, sizeof(top));
        n = stats_top(t2, STATS_BY_BYTES, top, 2);
        assert(n == 2);
        assert(strcmp(top[0].key, "/big")    == 0);
        assert(strcmp(top[1].key, "/medium") == 0);
        for (size_t i = 0; i < n; ++i) free(top[i].key);

        stats_destroy(t2);
    }

    /* --- merge большой таблицы (провоцируем pre-rehash) --- */
    {
        stats_table_t *a = stats_create();
        stats_table_t *b = stats_create();
        assert(a && b);

        char buf[32];
        for (int i = 0; i < 200; ++i) {
            int n = snprintf(buf, sizeof(buf), "/k-%d", i);
            assert(stats_add(b, buf, (size_t)n, 7) == 0);
        }
        assert(stats_merge(a, b) == 0);
        assert(stats_total_bytes(a) == 200 * 7);

        stats_destroy(a);
        stats_destroy(b);
    }

    printf("stats_test: passed\n");
    return 0;
}
