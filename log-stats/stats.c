#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>

#include "stats.h"

/*
 * Хеш-таблица с открытой адресацией и линейным пробированием.
 *
 *  - "Открытая адресация" значит: данные лежат прямо в массиве `buckets`,
 *    никаких связных списков. При коллизии (два ключа хешируются в один
 *    индекс) мы просто берём следующий слот, потом следующий, и так пока
 *    не найдём либо свободный, либо тот же ключ.
 *
 *  - "Линейное пробирование" — самый простой вариант: шаг = 1.
 *
 *  - capacity всегда степень двойки.  Это даёт быструю замену операции
 *    `hash % capacity` на `hash & (capacity - 1)` — эквивалентно, но без
 *    деления.  Деление целых на современных CPU стоит ~20 тактов,
 *    битовое AND — 1 такт.
 *
 *  - При load factor > 0.7 удваиваем capacity и переcleshuffle (rehash)
 *    всё содержимое.  0.7 — стандартный компромисс между «не тратить
 *    память впустую» и «не страдать от длинных проб-цепочек».
 */

#define INITIAL_CAPACITY 16 /* должно быть степенью двойки */
#define LOAD_FACTOR_NUM 7   /* load factor 0.7 = 7/10      */
#define LOAD_FACTOR_DEN 10

/*
 * Один слот таблицы.
 *
 * Договорённость: `key == NULL` означает «слот пустой».  Так нам не
 * нужно отдельное поле «занят / свободен».
 *
 * Поле `hash` кешируем в записи.  Зачем?
 *   1) При коллизиях быстро отсеиваем неподходящие записи: если хеши
 *      разные — ключи точно разные, можно пропустить дорогой memcmp.
 *   2) При rehash'е (когда удваиваем размер) не пересчитываем хеши
 *      строк — берём готовое.
 */
struct entry {
    char* key;
    size_t key_len;
    uint64_t hash;
    uint64_t count;
    uint64_t bytes;
};

struct stats_table {
    struct entry* buckets;
    size_t capacity; /* всегда степень двойки */
    size_t size;     /* сколько слотов занято */
};

/*
 * FNV-1a, 64-bit.
 *
 * Простая, быстрая, без зависимостей хеш-функция.  Алгоритм:
 *   h = OFFSET_BASIS;
 *   for each byte b: h = (h ^ b) * FNV_PRIME;
 *
 * Цифры взяты из спецификации FNV — это специально подобранные простые
 * числа, дающие хорошее «перемешивание» битов.
 */
static uint64_t fnv1a_64(const void* buf, size_t len) {
    const uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;
    const uint64_t FNV_PRIME = 0x100000001b3ULL;

    const unsigned char* p = (const unsigned char*)buf;
    uint64_t h = FNV_OFFSET;
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

/*
 * Найти слот для ключа в массиве `buckets` размером `cap`.
 *
 * Возвращает указатель на слот, который ЛИБО:
 *   - содержит этот же ключ (тогда вызывающий просто обновит count/bytes), ЛИБО
 *   - пустой (тогда вызывающий запишет туда новый ключ).
 *
 * Различить эти два случая в вызывающем коде просто:  slot->key == NULL?
 *
 * Алгоритм:
 *   idx = hash & (cap - 1)         — стартовая позиция (mask-трюк)
 *   пока buckets[idx] занят и ключ не совпадает: idx = (idx + 1) & mask
 *   возвращаем &buckets[idx]
 *
 * Линейное пробирование гарантированно найдёт либо нужный ключ, либо
 * пустой слот, ПРИ УСЛОВИИ что таблица не заполнена под завязку — и
 * именно поэтому мы держим load factor < 1.
 *
 * NB: сначала сравниваем hash (быстро), потом длину, и лишь потом memcmp
 * (дорого) — порядок важен для производительности.
 */
static struct entry* find_slot(struct entry* buckets, size_t cap,
                               const char* key, size_t key_len, uint64_t hash) {
    size_t mask = cap - 1;
    size_t idx = (size_t)hash & mask;
    while (buckets[idx].key != NULL) {
        if (buckets[idx].hash == hash && buckets[idx].key_len == key_len &&
            memcmp(buckets[idx].key, key, key_len) == 0) {
            return &buckets[idx]; /* нашли существующий */
        }
        idx = (idx + 1) & mask; /* пробируем дальше */
    }
    return &buckets[idx]; /* пустой — сюда вставлять */
}

/*
 * Создать пустую таблицу с начальной capacity = INITIAL_CAPACITY.
 *
 * calloc обнуляет память — все указатели key получаются NULL,
 * т.е. все слоты помечены как «пустые». Это ровно то, что надо.
 */
stats_table_t* stats_create(void) {
    stats_table_t* t = calloc(1, sizeof(*t));
    if (!t) return NULL;

    t->buckets = calloc(INITIAL_CAPACITY, sizeof(struct entry));
    if (!t->buckets) {
        free(t);
        return NULL;
    }
    t->capacity = INITIAL_CAPACITY;
    t->size = 0;
    return t;
}

/*
 * Освободить таблицу. Каждый ключ был выделен через malloc в stats_add(),
 * поэтому каждый освобождаем индивидуально.
 */
void stats_destroy(stats_table_t* t) {
    if (!t) return;
    for (size_t i = 0; i < t->capacity; ++i) {
        free(t->buckets[i].key);
    }
    free(t->buckets);
    free(t);
}

/*
 * Расширить таблицу: создать новый массив вдвое больше и перенести в
 * него все непустые слоты. Хеши уже посчитаны — берём из entry.hash.
 *
 * Возвращает 0 / -1 (OOM).
 */
static int rehash(stats_table_t* t) {
    size_t new_cap = t->capacity * 2;
    struct entry* new_buckets = calloc(new_cap, sizeof(struct entry));
    if (!new_buckets) return -1;

    for (size_t i = 0; i < t->capacity; ++i) {
        struct entry* e = &t->buckets[i];
        if (e->key == NULL) continue;
        struct entry* slot =
            find_slot(new_buckets, new_cap, e->key, e->key_len, e->hash);
        *slot = *e; /* перенесли запись целиком */
    }

    free(t->buckets);
    t->buckets = new_buckets;
    t->capacity = new_cap;
    return 0;
}

int stats_add(stats_table_t* t, const char* key, size_t key_len,
              uint64_t bytes) {
    if ((t->size + 1) * LOAD_FACTOR_DEN > t->capacity * LOAD_FACTOR_NUM) {
        if (rehash(t) == -1) return -1;
    }

    uint64_t hash = fnv1a_64(key, key_len);
    struct entry* slot = find_slot(t->buckets, t->capacity, key, key_len, hash);
    if (slot->key == NULL) {
        slot->key = malloc(key_len + 1);
        if (!slot->key) return -1;
        memcpy(slot->key, key, key_len);
        slot->key[key_len] = '\0';
        slot->key_len = key_len;
        slot->hash = hash;
        slot->count = 1;
        slot->bytes = bytes;
        t->size++;
    } else {
        slot->count += 1;
        slot->bytes += bytes;
    }
    return 0;
}

int stats_merge(stats_table_t* dst, const stats_table_t* src) {
    while ((dst->size + src->size) * LOAD_FACTOR_DEN >
           dst->capacity * LOAD_FACTOR_NUM) {
        if (rehash(dst) == -1) return -1;
    }

    for (size_t i = 0; i < src->capacity; ++i) {
        struct entry* e = &src->buckets[i];
        if (e->key == NULL) continue;
        struct entry* slot =
            find_slot(dst->buckets, dst->capacity, e->key, e->key_len, e->hash);
        if (slot->key == NULL) {
            slot->key = malloc(e->key_len + 1);
            if (!slot->key) return -1;
            memcpy(slot->key, e->key, e->key_len);
            slot->key[e->key_len] = '\0';
            slot->key_len = e->key_len;
            slot->hash = e->hash;
            slot->count = e->count;
            slot->bytes = e->bytes;
            dst->size++;
        } else {
            slot->count += e->count;
            slot->bytes += e->bytes;
        }
    }
    return 0;
}

/*
 * TODO: реализовать.
 *
 * Идея: собрать УКАЗАТЕЛИ на все занятые entry в массив, отсортировать
 * qsort'ом, скопировать первые top_n в out[] (с дублированием ключа).
 *
 * Шаги:
 *   1. Собрать массив pointers: const struct entry **arr =
 *        malloc(t->size * sizeof(*arr));
 *      Пройтись по t->buckets и сложить туда указатели на занятые слоты.
 *
 *   2. Объявить компаратор:
 *        static int cmp_by_bytes(const void *a, const void *b) { ... }
 *        static int cmp_by_count(const void *a, const void *b) { ... }
 *      В qsort кладём массив УКАЗАТЕЛЕЙ, поэтому в компараторе:
 *        const struct entry *ea = *(const struct entry *const *)a;
 *      Сравнение по убыванию: если ea->bytes > eb->bytes — вернуть -1.
 *
 *   3. qsort(arr, t->size, sizeof(*arr), by == STATS_BY_BYTES
 *                                          ? cmp_by_bytes : cmp_by_count);
 *
 *   4. n = (t->size < top_n) ? t->size : top_n;
 *      for (i = 0; i < n; ++i):
 *        out[i].key   = strndup(arr[i]->key, arr[i]->key_len);
 *        out[i].count = arr[i]->count;
 *        out[i].bytes = arr[i]->bytes;
 *
 *   5. free(arr); return n;
 *
 * NB: strndup даст вызывающему его собственный '\0'-terminated ключ,
 * который тот освободит через free(out[i].key) — как и описано в .h.
 */
static int cmp_by_bytes(const void* a, const void* b) {
    const struct entry* ea = *(const struct entry* const*)a;
    const struct entry* eb = *(const struct entry* const*)b;
    return (ea->bytes > eb->bytes) ? -1 : (ea->bytes < eb->bytes) ? 1 : 0;
}

static int cmp_by_count(const void* a, const void* b) {
    const struct entry* ea = *(const struct entry* const*)a;
    const struct entry* eb = *(const struct entry* const*)b;
    return (ea->count > eb->count) ? -1 : (ea->count < eb->count) ? 1 : 0;
}

size_t stats_top(const stats_table_t* t, stats_sort_t by, stats_entry_t* out,
                 size_t top_n) {
    const struct entry** arr = malloc(t->size * sizeof(*arr));
    if (!arr) return 0;
    size_t idx = 0;
    for (size_t i = 0; i < t->capacity; ++i)
        if (t->buckets[i].key) arr[idx] = &t->buckets[i], idx++;

    if (by == STATS_BY_BYTES) {
        qsort(arr, t->size, sizeof(*arr), cmp_by_bytes);
    }

    if (by == STATS_BY_COUNT) {
        qsort(arr, t->size, sizeof(*arr), cmp_by_count);
    }
    size_t n = (t->size < top_n) ? t->size : top_n;
    for (size_t i = 0; i < n; ++i) {
        out[i].key = strndup(arr[i]->key, arr[i]->key_len);
        out[i].count = arr[i]->count;
        out[i].bytes = arr[i]->bytes;
    }
    free(arr);
    return n;
}

/*
 * Простая O(capacity) сумма. Можно было бы держать total в struct и
 * обновлять в stats_add — но это лишний state, который легко рассинхрить.
 * Пока обойдёмся проходом по таблице.
 */
uint64_t stats_total_bytes(const stats_table_t* t) {
    uint64_t total = 0;
    for (size_t i = 0; i < t->capacity; ++i) {
        if (t->buckets[i].key) total += t->buckets[i].bytes;
    }
    return total;
}
