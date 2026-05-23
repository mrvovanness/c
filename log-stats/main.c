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
#include "util.h"

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
    pthread_mutex_t mu;      /* первым полем — структура для многопоточки */
    char**          files;
    size_t          n_files;
    size_t          idx;     /* индекс следующего невзятого файла */
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
 * Один поток-обработчик.  Берёт путь из очереди и обрабатывает файл,
 * пока очередь не опустеет.  Результат накапливается в c->urls / c->refs;
 * главный поток заберёт их после pthread_join через stats_merge.
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
        FREE_NULL(top[i].key);
    }

    printf("\nTop %d Referers by request count:\n", TOP_N);
    n = stats_top(refs, STATS_BY_COUNT, top, TOP_N);
    for (size_t i = 0; i < n; ++i) {
        printf("  %2zu. %12" PRIu64 " requests  %s\n", i + 1, top[i].count,
               top[i].key);
        FREE_NULL(top[i].key);
    }
}

int main(int argc, char* argv[]) {
    int                rc        = EXIT_FAILURE;
    int                nthreads  = 0;
    char**             files     = NULL;
    size_t             n_files   = 0;
    stats_table_t*     urls      = NULL;
    stats_table_t*     refs      = NULL;
    pthread_t*         tids      = NULL;
    struct worker_ctx* ctxs      = NULL;
    int                ctxs_made = 0;  /* сколько ctxs[i] полностью инициализировано */
    int                spawned   = 0;  /* сколько потоков реально запущено */
    struct file_queue  q         = { .mu = PTHREAD_MUTEX_INITIALIZER };

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <log_dir> <nthreads>\n", argv[0]);
        goto out;
    }
    if (parse_nthreads(argv[2], &nthreads) < 0) {
        fprintf(stderr, "invalid nthreads: '%s' (expected 1..%d)\n",
                argv[2], MAX_THREADS);
        goto out;
    }
    if (collect_files(argv[1], &files, &n_files) < 0) goto out;

    if (n_files == 0) {
        printf("Directory '%s' contains no files — nothing to process.\n",
               argv[1]);
        rc = EXIT_SUCCESS;
        goto out;
    }

    /* нет смысла запускать больше потоков, чем есть файлов */
    if ((size_t)nthreads > n_files) nthreads = (int)n_files;

    urls = stats_create();
    refs = stats_create();
    if (!urls || !refs) {
        fprintf(stderr, "out of memory\n");
        goto out;
    }

    q.files   = files;
    q.n_files = n_files;
    q.idx     = 0;

    tids = calloc((size_t)nthreads, sizeof(*tids));
    ctxs = calloc((size_t)nthreads, sizeof(*ctxs));
    if (!tids || !ctxs) {
        fprintf(stderr, "out of memory\n");
        goto out;
    }

    for (int i = 0; i < nthreads; ++i) {
        ctxs[i].queue = &q;
        ctxs[i].urls  = stats_create();
        ctxs[i].refs  = stats_create();
        if (!ctxs[i].urls || !ctxs[i].refs) {
            fprintf(stderr, "out of memory\n");
            goto out;
        }
        ctxs_made = i + 1;
    }

    for (int i = 0; i < nthreads; ++i) {
        int err = pthread_create(&tids[i], NULL, worker, &ctxs[i]);
        if (err != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            break;
        }
        spawned = i + 1;
    }

    for (int i = 0; i < spawned; ++i) {
        pthread_join(tids[i], NULL);
        stats_merge(urls, ctxs[i].urls);
        stats_merge(refs, ctxs[i].refs);
    }

    /* если хоть один worker отработал — данные полные (очередь общая) */
    if (spawned > 0) {
        print_results(urls, refs);
        rc = EXIT_SUCCESS;
    }

out:
    for (int i = 0; i < ctxs_made; ++i) {
        stats_destroy(ctxs[i].urls);
        stats_destroy(ctxs[i].refs);
    }
    free(tids);
    free(ctxs);
    pthread_mutex_destroy(&q.mu);
    stats_destroy(urls);
    stats_destroy(refs);
    if (files) {
        for (size_t i = 0; i < n_files; ++i) free(files[i]);
        free(files);
    }
    return rc;
}
