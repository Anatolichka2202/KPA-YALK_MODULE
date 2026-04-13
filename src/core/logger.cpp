#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

static orbita_log_level_t g_log_level = LOG_LEVEL_INFO;
static FILE* g_log_file = nullptr;

void orbita_log_set_level(orbita_log_level_t level) {
    g_log_level = level;
}

void orbita_log_set_file(const char* filename) {
    if (g_log_file && g_log_file != stderr) fclose(g_log_file);
    if (filename && filename[0]) {
        g_log_file = fopen(filename, "a");
        if (!g_log_file) g_log_file = stderr;
    } else {
        g_log_file = stderr;
    }
}

void orbita_log(orbita_log_level_t level, const char* file, int line, const char* fmt, ...) {
    if (level > g_log_level) return;
    if (!g_log_file) g_log_file = stderr;

    const char* level_str = "UNKNOWN";
    switch (level) {
    case LOG_LEVEL_ERROR:   level_str = "ERROR"; break;
    case LOG_LEVEL_WARNING: level_str = "WARN";  break;
    case LOG_LEVEL_INFO:    level_str = "INFO";  break;
    case LOG_LEVEL_DEBUG:   level_str = "DEBUG"; break;
    }

    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(g_log_file, "%s [%s] %s:%d: ", time_buf, level_str, file, line);
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log_file, fmt, args);
    va_end(args);
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
}
