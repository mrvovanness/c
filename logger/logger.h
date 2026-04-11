#ifndef LOGGER_H
#define LOGGER_H

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
} log_level_t;

int  logger_init(const char *filepath);
void logger_shutdown(void);
void logger_set_level(log_level_t level);

void logger_log(log_level_t level, const char *file, int line,
                const char *func, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 5, 6)))
#endif
;

#define LOG_DEBUG(...)   \
    logger_log(LOG_DEBUG,   __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_INFO(...)    \
    logger_log(LOG_INFO,    __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_WARNING(...) \
    logger_log(LOG_WARNING, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_ERROR(...)   \
    logger_log(LOG_ERROR,   __FILE__, __LINE__, __func__, __VA_ARGS__)

#endif /* LOGGER_H */
