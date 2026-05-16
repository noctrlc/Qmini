#ifndef LOGGER_H
#define LOGGER_H

#include <windows.h>

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
} log_level_t;

int  log_init(const char *path);
void log_shutdown(void);
void log_set_level(log_level_t level);
void log_write(log_level_t level, const char *fmt, ...);

#define LOG_DEBUG(fmt, ...) log_write(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  log_write(LOG_INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  log_write(LOG_WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_write(LOG_ERROR, fmt, ##__VA_ARGS__)

#endif
