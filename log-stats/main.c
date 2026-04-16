#define _POSIX_C_SOURCE 200809L

#include "parser.h"
#include "stats.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_PATH_LEN   4096
#define MAX_THREADS    1024

static int parse_nthreads(const char *s, int *out)
{
    char *end = NULL;
    long  v   = strtol(s, &end, 10);
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
static int collect_files(const char *dir, char ***files, size_t *n_files)
{
    DIR            *dp     = NULL;
    struct dirent  *entry  = NULL;
    char          **arr    = NULL;
    size_t          n      = 0;
    size_t          cap    = 0;
    int             rc     = -1;

    dp = opendir(dir);
    if (!dp) {
        fprintf(stderr, "opendir %s: %s\n", dir, strerror(errno));
        goto out;
    }

    errno = 0;
    while ((entry = readdir(dp)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        char path[MAX_PATH_LEN];
        int  np = snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        if (np < 0 || (size_t)np >= sizeof(path)) {
            fprintf(stderr, "path too long: %s/%s\n", dir, entry->d_name);
            continue;
        }

        struct stat st = {0};
        if (stat(path, &st) < 0) {
            fprintf(stderr, "stat %s: %s\n", path, strerror(errno));
            continue;
        }
        if (!S_ISREG(st.st_mode))
            continue;

        if (n == cap) {
            size_t  new_cap = cap ? cap * 2 : 16;
            char  **new_arr = realloc(arr, new_cap * sizeof(*new_arr));
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

    *files   = arr;
    *n_files = n;
    arr      = NULL;   /* ownership transferred */
    rc = 0;
out:
    if (dp)
        closedir(dp);
    if (arr) {
        for (size_t i = 0; i < n; ++i)
            free(arr[i]);
        free(arr);
    }
    return rc;
}

int main(int argc, char *argv[])
{
    int      rc       = EXIT_FAILURE;
    int      nthreads = 0;
    char   **files    = NULL;
    size_t   n_files  = 0;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <log_dir> <nthreads>\n", argv[0]);
        goto out;
    }
    const char *dir = argv[1];

    if (parse_nthreads(argv[2], &nthreads) < 0) {
        fprintf(stderr, "invalid nthreads: '%s' (expected 1..%d)\n",
                argv[2], MAX_THREADS);
        goto out;
    }

    if (collect_files(dir, &files, &n_files) < 0)
        goto out;

    if (n_files == 0) {
        printf("Directory '%s' contains no files — nothing to process.\n", dir);
        rc = EXIT_SUCCESS;
        goto out;
    }

    /* TODO: spin up workers, process, reduce, print results. */
    printf("Found %zu file(s), using %d thread(s):\n", n_files, nthreads);
    for (size_t i = 0; i < n_files; ++i)
        printf("  %s\n", files[i]);

    /* Silence unused-symbol warnings while the rest is stubbed. */
    (void)parse_combined;
    (void)stats_create;
    (void)stats_destroy;
    (void)stats_add;
    (void)stats_merge;
    (void)stats_top;
    (void)stats_total_bytes;

    rc = EXIT_SUCCESS;
out:
    if (files) {
        for (size_t i = 0; i < n_files; ++i)
            free(files[i]);
        free(files);
    }
    exit(rc);
}
