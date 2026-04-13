/**
 * @file logger.h
 * @brief Внутреннее логирование.
 *
 * Уровни: ERROR, WARNING, INFO, DEBUG.
 * Вывод в stderr (по умолчанию) и/или в файл.
 */

#ifndef ORBITA_LOGGER_H
#define ORBITA_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
} orbita_log_level_t;

// Установка уровня логирования (по умолчанию INFO)
void orbita_log_set_level(orbita_log_level_t level);

// Установка файла для вывода (если NULL, только stderr)
void orbita_log_set_file(const char* filename);

// Внутренние макросы (используются в коде)
#define LOG_ERROR(fmt, ...) orbita_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) orbita_log(LOG_LEVEL_WARNING, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) orbita_log(LOG_LEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) orbita_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

// Функция логирования (не вызывать напрямую)
void orbita_log(orbita_log_level_t level, const char* file, int line, const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_LOGGER_H
