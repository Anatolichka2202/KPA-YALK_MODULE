/**
 * @file orbita_config.h
 * @brief Загрузка конфигурационных файлов (INI, JSON).
 *
 * Поддерживает чтение настроек периферии:
 * - IP-адреса устройств (имитатор, источник питания)
 * - Идентификаторы VISA (вольтметр, генератор)
 * - COM-порты
 * - Калибровочные коэффициенты
 *
 * Формат INI: секции [Device], [Settings] и т.д.
 */

#ifndef ORBITA_CONFIG_H
#define ORBITA_CONFIG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* orbita_config_t;

// Загрузить конфигурацию из INI-файла
orbita_config_t orbita_config_load_ini(const char* filename);
// Загрузить из JSON
orbita_config_t orbita_config_load_json(const char* filename);
void orbita_config_free(orbita_config_t cfg);

// Получить строковое значение по ключу (секция.ключ)
const char* orbita_config_get_string(orbita_config_t cfg, const char* key, const char* default_value);
int orbita_config_get_int(orbita_config_t cfg, const char* key, int default_value);
double orbita_config_get_double(orbita_config_t cfg, const char* key, double default_value);

// Проверка существования ключа
int orbita_config_has_key(orbita_config_t cfg, const char* key);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_CONFIG_H
