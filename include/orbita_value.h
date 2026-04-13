/**
 * @file orbita_value.h
 * @brief Преобразование 12‑битных слов в физические значения.
 *
 * По типу адреса (orbita_addr_type_t) и битовой маске вычисляет:
 * - аналоговое напряжение (0..1023 → 0..10В? – масштаб задаётся отдельно)
 * - контактный сигнал (0 или 1)
 * - быстрые параметры (T21, T22, T24)
 * - температуру (код → градусы, коэффициент задаётся)
 *
 * Функции работают с сырыми словами из декодера.
 */

#ifndef ORBITA_VALUE_H
#define ORBITA_VALUE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Преобразование аналогового 10‑битного значения (T01) в напряжение (В)
// scale – коэффициент пересчёта (например, 10.0 / 1023.0)
double orbita_analog_10bit_to_voltage(uint16_t word, double scale);

// Преобразование аналогового 9‑битного значения (T01-01) в напряжение
double orbita_analog_9bit_to_voltage(uint16_t word, double scale);

// Извлечение контактного бита (T05). bitNum = 1..10
int orbita_contact_extract_bit(uint16_t word, int bitNum);

// Быстрый параметр T21: 8 бит (0..255)
int orbita_fast_t21_value(uint16_t word);

// Быстрый параметр T22: 6 бит, собирается из двух слов (первое и второе)
int orbita_fast_t22_value(uint16_t first_word, uint16_t second_word);

// Быстрый параметр T24: 6 бит, аналогично T22
int orbita_fast_t24_value(uint16_t first_word, uint16_t second_word);

// Температурный код T11: 8 бит (0..255)
int orbita_temperature_code(uint16_t word);

// Преобразование кода температуры в градусы Цельсия (линейное)
double orbita_temperature_code_to_celsius(int code, double offset, double scale);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_VALUE_H
