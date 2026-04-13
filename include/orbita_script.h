/**
 * @file orbita_script.h
 * @brief Lua-скриптинг для автоматизации тестов.
 *
 * Позволяет загружать Lua-скрипты, регистрировать функции API:
 * - orbita_set_isd_channel(channel, value)
 * - orbita_read_voltmeter()
 * - orbita_set_power_supply(volt, curr)
 * - orbita_get_channel_value(address_string)
 * - orbita_sleep_ms(ms)
 *
 * Скрипты могут запускаться как из библиотеки, так и из внешнего приложения.
 */

#ifndef ORBITA_SCRIPT_H
#define ORBITA_SCRIPT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* orbita_script_engine_t;

// Создание/уничтожение движка (привязывается к контексту orbita_handle_t)
orbita_script_engine_t orbita_script_engine_create(void* orbita_context);
void orbita_script_engine_destroy(orbita_script_engine_t eng);

// Загрузка и выполнение скрипта (файл .lua)
int orbita_script_engine_dofile(orbita_script_engine_t eng, const char* filename);

// Вызов глобальной Lua-функции с аргументами JSON, результат в виде JSON-строки (освобождать free)
char* orbita_script_engine_call(orbita_script_engine_t eng, const char* func_name, const char* args_json);

// Регистрация дополнительных C-функций в Lua (для расширения API)
int orbita_script_engine_register(orbita_script_engine_t eng, const char* name, void* c_function);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_SCRIPT_H
