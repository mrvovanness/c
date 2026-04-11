#define _POSIX_C_SOURCE 200809L // for getpid() и pthread, must be before any #include

#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#if defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>
#endif

#define MAX_BACKTRACE_DEPTH 64
#define LOG_BUF_SIZE 4096

static FILE            *g_logfile   = NULL;
static log_level_t      g_min_level = LOG_DEBUG;
static pthread_mutex_t  g_mutex     = PTHREAD_MUTEX_INITIALIZER;

static const char *level_to_str(log_level_t level)
{
    switch (level) {
    case LOG_DEBUG:   return "DEBUG";
    case LOG_INFO:    return "INFO";
    case LOG_WARNING: return "WARNING";
    case LOG_ERROR:   return "ERROR";
    }
    return "UNKNOWN";
}

static void dump_backtrace(FILE *f)
{
#if defined(__APPLE__) || defined(__linux__)
    void *frames[MAX_BACKTRACE_DEPTH];
    int   n = backtrace(frames, MAX_BACKTRACE_DEPTH);

    fprintf(f, "  --- stack trace (%d frames) ---\n", n);

    for (int i = 0; i < n; ++i) {
        char cmd[256];
#if defined(__APPLE__)
        snprintf(cmd, sizeof(cmd), "atos -p %d %p 2>/dev/null", (int)getpid(), frames[i]);
#else
        snprintf(cmd, sizeof(cmd), "addr2line -e /proc/self/exe -f -p %p 2>/dev/null", frames[i]);
#endif
        FILE *p = popen(cmd, "r");
        if (p != NULL) {
            char line[512];
            if (fgets(line, (int)sizeof(line), p) != NULL) {
                fprintf(f, "  %d: %s", i, line);
                if (line[0] != '\0' && line[strlen(line) - 1] != '\n') {
                    fputc('\n', f);
                }
            }
            pclose(p);
        } else {
            fprintf(f, "  %d: %p\n", i, frames[i]);
        }
    }

    fprintf(f, "  --- end stack trace ---\n");
#else
    fprintf(f, "  (stack trace not available on this platform)\n");
#endif
}

int logger_init(const char *filepath)
{
    pthread_mutex_lock(&g_mutex);

    if (g_logfile != NULL && g_logfile != stderr) {
        fclose(g_logfile);
        g_logfile = NULL;
    }

    g_logfile = fopen(filepath, "a");
    if (g_logfile == NULL) {
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    setvbuf(g_logfile, NULL, _IOLBF, 0);

    pthread_mutex_unlock(&g_mutex);
    return 0;
}

void logger_shutdown(void)
{
    pthread_mutex_lock(&g_mutex);

    if (g_logfile != NULL && g_logfile != stderr) {
        fclose(g_logfile);
    }
    g_logfile = NULL;

    pthread_mutex_unlock(&g_mutex);
}

void logger_set_level(log_level_t level)
{
    pthread_mutex_lock(&g_mutex);
    g_min_level = level;
    pthread_mutex_unlock(&g_mutex);
}

void logger_log(log_level_t level, const char *file, int line,
                const char *func, const char *fmt, ...)
{
    pthread_mutex_lock(&g_mutex);

    if (g_logfile == NULL || level < g_min_level) {
        pthread_mutex_unlock(&g_mutex);
        return;
    }

    time_t     now = time(NULL);
    struct tm  tm_buf;
    localtime_r(&now, &tm_buf);

    char buf[LOG_BUF_SIZE];
    int  off = snprintf(buf, sizeof(buf),
                        "%04d-%02d-%02d %02d:%02d:%02d [%-7s] %s:%d (%s): ",
                        tm_buf.tm_year + 1900, tm_buf.tm_mon + 1,
                        tm_buf.tm_mday, tm_buf.tm_hour, tm_buf.tm_min,
                        tm_buf.tm_sec, level_to_str(level), file, line, func);

    if (off < 0) {
        off = 0;
    }

    if ((size_t)off < sizeof(buf) - 1) {
        va_list ap;
        va_start(ap, fmt);
        int written = vsnprintf(buf + off, sizeof(buf) - (size_t)off, fmt, ap);
        va_end(ap);
        if (written > 0) {
            off += written;
            if ((size_t)off >= sizeof(buf)) {
                off = (int)(sizeof(buf) - 1);
            }
        }
    }

    if (off > 0 && buf[off - 1] != '\n') {
        if ((size_t)off < sizeof(buf) - 1) {
            buf[off++] = '\n';
        }
        buf[off] = '\0';
    }

    fputs(buf, g_logfile);

    if (level == LOG_ERROR) {
        dump_backtrace(g_logfile);
    }

    fflush(g_logfile);
    pthread_mutex_unlock(&g_mutex);
}
