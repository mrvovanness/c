#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "parser.h"
#include "stats.h"

#define MAX_PATH_LEN 4096
#define MAX_THREADS 1024
#define TOP_N 10

static int parse_nthreads(const char* s, int* out) {
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 1 || v > MAX_THREADS) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

/*
 * Collect regular files directly under `dir`; write the array and count
 * through the caller-supplied out-parameters.
 * Hidden entries (starting with '.') are skipped; subdirectories are not
 * traversed recursively. On success ownership of the array and each
 * string is transferred to the caller.
 */
static int collect_files(const char* dir, char*** files, size_t* n_files) {
    DIR* dp = NULL;
    struct dirent* entry = NULL;
    char** arr = NULL;
    size_t n = 0;
    size_t cap = 0;
    int rc = -1;

    dp = opendir(dir);
    if (!dp) {
        fprintf(stderr, "opendir %s: %s\n", dir, strerror(errno));
        goto out;
    }

    errno = 0;
    while ((entry = readdir(dp)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char path[MAX_PATH_LEN];
        int np = snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        if (np < 0 || (size_t)np >= sizeof(path)) {
            fprintf(stderr, "path too long: %s/%s\n", dir, entry->d_name);
            continue;
        }

        struct stat st = {0};
        if (stat(path, &st) < 0) {
            fprintf(stderr, "stat %s: %s\n", path, strerror(errno));
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;

        if (n == cap) {
            size_t new_cap = cap ? cap * 2 : 16;
            char** new_arr = realloc(arr, new_cap * sizeof(*new_arr));
            if (!new_arr) {
                fprintf(stderr, "out of memory\n");
                goto out;
            }
            arr = new_arr;
            cap = new_cap;
        }

        arr[n] = strdup(path);
        if (!arr[n]) {
            fprintf(stderr, "out of memory\n");
            goto out;
        }
        n++;

        errno = 0;
    }
    if (errno != 0) {
        fprintf(stderr, "readdir %s: %s\n", dir, strerror(errno));
        goto out;
    }

    *files = arr;
    *n_files = n;
    arr = NULL; /* ownership transferred */
    rc = 0;
out:
    if (dp) closedir(dp);
    if (arr) {
        for (size_t i = 0; i < n; ++i) free(arr[i]);
        free(arr);
    }
    return rc;
}

/*
 * Прочитать один файл построчно, распарсить combined-формат и
 * накопить статистику в двух таблицах:
 *   urls — агрегируем bytes (count тоже считается, но в выводе нам нужен bytes)
 *   refs — агрегируем по count (bytes игнорируем — передаём 0)
 *
 * Битые строки пропускаются молча: нормальные логи иногда содержат
 * мусор, не обязан портить общую статистику.
 *
 * Ошибка fopen — не фатал: печатаем в stderr и идём дальше. По
 * условиям задачи «корректно обрабатывать ошибки доступа к файлам» —
 * но это не значит «падать».
 */
static int process_file(const char* path, stats_table_t* urls,
                        stats_table_t* refs) {
    FILE* fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "fopen %s: %s\n", path, strerror(errno));
        return -1;
    }

    char* line = NULL;
    size_t cap = 0;
    ssize_t n;
    int rc = 0;

    while ((n = getline(&line, &cap, fp)) > 0) {
        /* срезаем '\n' и '\r' (если CRLF) */
        if (n > 0 && line[n - 1] == '\n') line[--n] = '\0';
        if (n > 0 && line[n - 1] == '\r') line[--n] = '\0';
        if (n == 0) continue;

        log_entry_t e = {0};
        if (parse_combined(line, (size_t)n, &e) != 0) {
            continue; /* битая строка — пропускаем */
        }

        if (stats_add(urls, e.url, e.url_len, e.bytes_sent) < 0) {
            fprintf(stderr, "stats_add(urls): out of memory\n");
            rc = -1;
            break;
        }
        if (stats_add(refs, e.referer, e.referer_len, 0) < 0) {
            fprintf(stderr, "stats_add(refs): out of memory\n");
            rc = -1;
            break;
        }
    }
    if (ferror(fp)) {
        fprintf(stderr, "read %s: %s\n", path, strerror(errno));
        rc = -1;
    }

    free(line);
    fclose(fp);
    return rc;
}

/*
 * Очередь файлов на обработку.  Producer (main) собирает все имена ДО
 * запуска потоков — поэтому никакого "конец очереди ещё не пришёл, жди".
 * Worker'у достаточно увидеть NULL и завершиться.
 *
 * Защищаем счётчик idx мьютексом — это единственное shared-состояние на
 * горячем пути.  Сами таблицы у каждого worker'а свои, так что
 * stats_add блокировок не требует.
 */
struct file_queue {
    char** files;
    size_t n_files;
    size_t idx; /* индекс следующего невзятого файла */
    pthread_mutex_t mu;
};

/*
 * Вытащить следующий путь из очереди.  NULL = файлы кончились.
 */
static const char* next_file(struct file_queue* q) {
    pthread_mutex_lock(&q->mu);
    const char* path = (q->idx < q->n_files) ? q->files[q->idx++] : NULL;
    pthread_mutex_unlock(&q->mu);
    return path;
}

/*
 * Контекст одного worker'а.  Каждый поток владеет СВОЕЙ парой таблиц —
 * никаких блокировок на stats_add. Указатель на общую очередь —
 * единственная точка контакта с другими потоками.
 *
 * После pthread_join главный поток сольёт urls/refs всех контекстов в
 * одну общую через stats_merge.
 */
struct worker_ctx {
    struct file_queue* queue;
    stats_table_t* urls;
    stats_table_t* refs;
};

/*
 * TODO: реализовать.
 *
 * Один поток-обработчик.  Цикл: брать путь из очереди и вызывать
 * process_file(path, c->urls, c->refs), пока next_file не вернёт NULL.
 *
 *   struct worker_ctx *c = arg;
 *   const char *path;
 *   while ((path = next_file(c->queue)) != NULL) {
 *       (void)process_file(path, c->urls, c->refs);
 *   }
 *   return NULL;
 *
 * Возвращаемое значение нам не нужно — результат накопится в c->urls
 * и c->refs, главный поток заберёт их после pthread_join.
 */
static void* worker(void* arg) {
    struct worker_ctx* c = arg;
    const char* path;
    while ((path = next_file(c->queue)) != NULL) {
        (void)process_file(path, c->urls, c->refs);
    }
    return NULL;
}

static void print_results(stats_table_t* urls, stats_table_t* refs) {
    stats_entry_t top[TOP_N] = {0};
    size_t n;

    printf("Total bytes: %" PRIu64 "\n\n", stats_total_bytes(urls));

    printf("Top %d URLs by traffic:\n", TOP_N);
    n = stats_top(urls, STATS_BY_BYTES, top, TOP_N);
    for (size_t i = 0; i < n; ++i) {
        printf("  %2zu. %12" PRIu64 " bytes  %s\n", i + 1, top[i].bytes,
               top[i].key);
        free(top[i].key);
    }

    printf("\nTop %d Referers by request count:\n", TOP_N);
    n = stats_top(refs, STATS_BY_COUNT, top, TOP_N);
    for (size_t i = 0; i < n; ++i) {
        printf("  %2zu. %12" PRIu64 " requests  %s\n", i + 1, top[i].count,
               top[i].key);
        free(top[i].key);
    }
}

int main(int argc, char* argv[]) {
    int rc = EXIT_FAILURE;
    int nthreads = 0;
    char** files = NULL;
    size_t n_files = 0;
    stats_table_t* urls = NULL;
    stats_table_t* refs = NULL;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <log_dir> <nthreads>\n", argv[0]);
        goto out;
    }
    const char* dir = argv[1];

    if (parse_nthreads(argv[2], &nthreads) < 0) {
        fprintf(stderr, "invalid nthreads: '%s' (expected 1..%d)\n", argv[2],
                MAX_THREADS);
        goto out;
    }

    if (collect_files(dir, &files, &n_files) < 0) goto out;

    if (n_files == 0) {
        printf("Directory '%s' contains no files — nothing to process.\n", dir);
        rc = EXIT_SUCCESS;
        goto out;
    }

    urls = stats_create();
    refs = stats_create();
    if (!urls || !refs) {
        fprintf(stderr, "out of memory\n");
        goto out;
    }

    /*
     * TODO: заменить однопоточный проход на пул потоков.
     *
     *  1. Если n_files < nthreads — урезать nthreads до n_files.
     *  2. Заинициализировать struct file_queue:
     *       q.files=files; q.n_files=n_files; q.idx=0;
     *       pthread_mutex_init(&q.mu, NULL);
     *  3. pthread_t          *tids = calloc(nthreads, sizeof(*tids));
     *     struct worker_ctx  *ctxs = calloc(nthreads, sizeof(*ctxs));
     *     для каждого ctxs[i]: queue=&q, urls=stats_create(),
     * refs=stats_create().
     *  4. for i in 0..nthreads:
     *       pthread_create(&tids[i], NULL, worker, &ctxs[i]);
     *     если pthread_create вернул != 0 — fail (или урезать nthreads до
     * уже созданного).
     *  5. for i in 0..nthreads: pthread_join(tids[i], NULL).
     *  6. for i in 0..nthreads:
     *       stats_merge(urls, ctxs[i].urls);
     *       stats_merge(refs, ctxs[i].refs);
     *       stats_destroy(ctxs[i].urls); stats_destroy(ctxs[i].refs);
     *     (общие urls/refs уже созданы выше пустыми — аккумулируем в них.)
     *  7. pthread_mutex_destroy(&q.mu);
     *     free(tids); free(ctxs);
     */
    if (n_files < (size_t)nthreads) nthreads = (int)n_files;
    struct file_queue q = {
        .files = files,
        .n_files = n_files,
        .idx = 0,
        .mu = PTHREAD_MUTEX_INITIALIZER,
    };

    pthread_t* tids = calloc(nthreads, sizeof(*tids));
    struct worker_ctx* ctxs = calloc(nthreads, sizeof(*ctxs));

    if (!tids || !ctxs) {
        fprintf(stderr, "out of memory\n");
        goto out;
    }
    for (int w_ctx = 0; w_ctx < nthreads; ++w_ctx) {
        ctxs[w_ctx].queue = &q;
        ctxs[w_ctx].urls = stats_create();
        ctxs[w_ctx].refs = stats_create();
        if (!ctxs[w_ctx].urls || !ctxs[w_ctx].refs) {
            fprintf(stderr, "out of memory\n");
            goto out;
        }
    }
    for (int i = 0; i < nthreads; ++i) {
        int err = pthread_create(&tids[i], NULL, worker, &ctxs[i]);
        if (err != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            nthreads = i;
            break;
        }
    }
    for (int i = 0; i < nthreads; ++i) {
        pthread_join(tids[i], NULL);
        stats_merge(urls, ctxs[i].urls);
        stats_merge(refs, ctxs[i].refs);
        stats_destroy(ctxs[i].urls);
        stats_destroy(ctxs[i].refs);
    }
    pthread_mutex_destroy(&q.mu);
    free(tids);
    free(ctxs);

    print_results(urls, refs);
    rc = EXIT_SUCCESS;
out:
    stats_destroy(urls);
    stats_destroy(refs);
    if (files) {
        for (size_t i = 0; i < n_files; ++i) free(files[i]);
        free(files);
    }
    exit(rc);
}
