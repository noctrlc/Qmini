#include "logger.h"
#include <stdio.h>
#include <stdarg.h>

static CRITICAL_SECTION g_log_lock;
static FILE           *g_log_file = NULL;
static log_level_t     g_log_level = LOG_INFO;
static int             g_initialized = 0;
static char            g_buf[4096];
static int             g_buf_pos = 0;
static DWORD           g_last_flush = 0;

static void log_flush(void) {
    if (g_buf_pos > 0 && g_log_file) {
        fwrite(g_buf, 1, g_buf_pos, g_log_file);
        fflush(g_log_file);
        g_buf_pos = 0;
    }
    g_last_flush = GetTickCount();
}

static const char *level_str(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO ";
        case LOG_WARN:  return "WARN ";
        case LOG_ERROR: return "ERROR";
        default:        return "???? ";
    }
}

int log_init(const char *path) {
    if (g_initialized) return 1;

    InitializeCriticalSection(&g_log_lock);

    if (path && path[0]) {
        g_log_file = fopen(path, "a");
    } else {
        /* Derive path from executable directory */
        char exe_path[MAX_PATH];
        char log_path[MAX_PATH];
        if (GetModuleFileNameA(NULL, exe_path, MAX_PATH)) {
            /* Strip executable name */
            char *last_slash = strrchr(exe_path, '\\');
            if (last_slash) {
                *(last_slash + 1) = 0;
                _snprintf(log_path, MAX_PATH, "%sqmini_log.txt", exe_path);
                g_log_file = fopen(log_path, "a");
            }
        }
        if (!g_log_file) {
            g_log_file = fopen("qmini_log.txt", "a");
        }
    }

    g_buf_pos = 0;
    g_last_flush = GetTickCount();
    g_initialized = 1;
    return g_log_file != NULL;
}

void log_shutdown(void) {
    if (!g_initialized) return;
    EnterCriticalSection(&g_log_lock);
    log_flush();
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    g_initialized = 0;
    LeaveCriticalSection(&g_log_lock);
    DeleteCriticalSection(&g_log_lock);
}

void log_set_level(log_level_t level) {
    g_log_level = level;
}

void log_write(log_level_t level, const char *fmt, ...) {
    if (!g_initialized) return;
    if (level < g_log_level) return;

    /* Build timestamp */
    SYSTEMTIME st;
    GetLocalTime(&st);
    char ts[32];
    _snprintf(ts, sizeof(ts), "[%02d:%02d:%02d.%03d]",
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    /* Format message */
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    int msg_len = _vsnprintf(msg, sizeof(msg) - 1, fmt, ap);
    va_end(ap);
    if (msg_len < 0) msg_len = sizeof(msg) - 2;
    if (msg_len >= (int)sizeof(msg) - 1) msg_len = (int)sizeof(msg) - 2;
    msg[msg_len] = 0;

    EnterCriticalSection(&g_log_lock);

    /* Format complete line */
    int line_len = _snprintf(g_buf + g_buf_pos,
                              sizeof(g_buf) - g_buf_pos,
                              "%s [%s] %s\n", ts, level_str(level), msg);

    if (line_len > 0 && g_buf_pos + line_len < (int)sizeof(g_buf)) {
        g_buf_pos += line_len;
    } else {
        /* Buffer full, flush first then try again */
        log_flush();
        line_len = _snprintf(g_buf, sizeof(g_buf), "%s [%s] %s\n",
                              ts, level_str(level), msg);
        if (line_len > 0 && line_len < (int)sizeof(g_buf))
            g_buf_pos = line_len;
    }

    /* Flush on error or if buffer is > 75% full */
    if (level >= LOG_ERROR || g_buf_pos > 3072)
        log_flush();

    LeaveCriticalSection(&g_log_lock);
}
