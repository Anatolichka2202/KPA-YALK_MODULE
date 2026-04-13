/**
 * @file orbita_core.h
 * @brief Внутреннее ядро библиотеки – реализация главного API.
 *
 * Содержит структуру orbita_context_t (opaque pointer для orbita_handle_t).
 * Управляет устройствами, декодером, менеджером адресов, пулом данных, TLM-записью.
 */

#ifndef ORBITA_CORE_H
#define ORBITA_CORE_H

#include "orbita.h"
#include "orbita_address.h"
#include "orbita_device.h"

#ifdef __cplusplus
extern "C" {
#endif

// Внутренний контекст (определение скрыто)
typedef struct orbita_context orbita_context_t;

// Создание/уничтожение (вызывается из orbita_create/destroy)
orbita_context_t* orbita_context_create(void);
void orbita_context_destroy(orbita_context_t* ctx);

// Настройка устройств
int orbita_context_set_device_e2010(orbita_context_t* ctx, int channel, double sample_rate_khz);
int orbita_context_set_device_limesdr(orbita_context_t* ctx, const char* params);
int orbita_context_set_voltmeter_visa(orbita_context_t* ctx, const char* resource);

// Адреса
int orbita_context_set_channels(orbita_context_t* ctx, const orbita_channel_desc_t* channels, int count);
int orbita_context_set_channels_from_file(orbita_context_t* ctx, const char* filename);

// Управление сбором
int orbita_context_start(orbita_context_t* ctx);
int orbita_context_stop(orbita_context_t* ctx);
int orbita_context_set_invert_signal(orbita_context_t* ctx, int invert);

// TLM
int orbita_context_start_recording(orbita_context_t* ctx, const char* filename);
int orbita_context_stop_recording(orbita_context_t* ctx);

// Ожидание данных
int orbita_context_wait_for_data(orbita_context_t* ctx, int timeout_ms);

// Получение значений (по индексу канала)
double orbita_context_get_analog(orbita_context_t* ctx, int idx);
int    orbita_context_get_contact(orbita_context_t* ctx, int idx);
int    orbita_context_get_fast(orbita_context_t* ctx, int idx);
int    orbita_context_get_temperature(orbita_context_t* ctx, int idx);
int    orbita_context_get_bus(orbita_context_t* ctx, int idx);

// Статистика
int orbita_context_get_phrase_error_percent(orbita_context_t* ctx);
int orbita_context_get_group_error_percent(orbita_context_t* ctx);

// Скрипты (прокси)
int orbita_context_load_script(orbita_context_t* ctx, const char* script_path);
int orbita_context_run_script_function(orbita_context_t* ctx, const char* func_name, const char* args_json, char** result_json);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_CORE_H
