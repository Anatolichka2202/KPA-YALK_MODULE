/**
 * @file orbita.h
 * @brief Главный API библиотеки сбора телеметрии "Орбита-IV".
 *
 * Объединяет все подсистемы: устройства, адреса, декодирование, TLM, скрипты.
 * Работа через непрозрачный указатель orbita_handle_t.
 *
 * Типовой сценарий:
 * 1. orbita_create()
 * 2. orbita_set_device_e2010() или orbita_set_device_limesdr()
 * 3. orbita_set_channels_from_file()
 * 4. orbita_start()
 * 5. В цикле: orbita_wait_for_data() -> orbita_get_analog() и т.д.
 * 6. orbita_stop(), orbita_destroy()
 */


#ifndef ORBITA_H
#define ORBITA_H

#include "orbita_address.h"
#include "orbita_device.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Контекст (opaque pointer)
typedef void* orbita_handle_t;

// Создание/уничтожение контекста
orbita_handle_t orbita_create(void);
void orbita_destroy(orbita_handle_t h);

// Настройка устройства (до вызова orbita_start)
int orbita_set_device_e2010(orbita_handle_t h, int channel, double sample_rate_khz);
int orbita_set_device_limesdr(orbita_handle_t h, const char* params); // params = "freq=2.2994e6,gain=40,..."
int orbita_set_voltmeter_visa(orbita_handle_t h, const char* resource); // опционально

// Загрузка адресов (очищает предыдущие)
int orbita_set_channels(orbita_handle_t h, const orbita_channel_desc_t* channels, int count);
// Удобная обёртка: загрузить из файла
int orbita_set_channels_from_file(orbita_handle_t h, const char* filename);

// Управление сбором
int orbita_start(orbita_handle_t h);
int orbita_stop(orbita_handle_t h);
int orbita_set_invert_signal(orbita_handle_t h, int invert); // 1 – инвертировать биты

// Запись TLM
int orbita_start_recording(orbita_handle_t h, const char* filename);
int orbita_stop_recording(orbita_handle_t h);

// Получение данных (блокирующее ожидание новых данных, timeout_ms)
// Возвращает 0 при успехе, -1 при ошибке, -2 по таймауту.
int orbita_wait_for_data(orbita_handle_t h, int timeout_ms);

// Доступ к текущим значениям каналов (по индексу в порядке загрузки)
double orbita_get_analog(orbita_handle_t h, int channel_index);
int    orbita_get_contact(orbita_handle_t h, int channel_index);
int    orbita_get_fast(orbita_handle_t h, int channel_index);
int    orbita_get_temperature(orbita_handle_t h, int channel_index);
int    orbita_get_bus(orbita_handle_t h, int channel_index);

// Статистика ошибок синхронизации (проценты)
int orbita_get_phrase_error_percent(orbita_handle_t h);
int orbita_get_group_error_percent(orbita_handle_t h);

// Управление Lua-скриптами
int orbita_load_script(orbita_handle_t h, const char* script_path);
int orbita_run_script_function(orbita_handle_t h, const char* func_name, const char* args_json, char** result_json);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_H
