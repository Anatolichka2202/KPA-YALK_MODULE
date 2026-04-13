/**
 * @file value_converter.h
 * @brief Фасад для преобразования 12-битных слов в физические величины.
 */

#ifndef ORBITA_VALUE_CONVERTER_H
#define ORBITA_VALUE_CONVERTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Преобразование аналогового 10-битного значения (T01) в напряжение (В)
// word - 12-битное слово (младшие 10 бит – данные)
// scale - коэффициент пересчёта (например, 10.0 / 1023.0)
double orbita_analog_10bit_to_voltage(uint16_t word, double scale);

// Преобразование аналогового 9-битного значения (T01-01) в напряжение (В)
double orbita_analog_9bit_to_voltage(uint16_t word, double scale);

// Извлечение контактного бита (T05). bitNum = 1..10
int orbita_contact_extract_bit(uint16_t word, int bitNum);

// Быстрый параметр T21: 8 бит (0..255)
int orbita_fast_t21_value(uint16_t word);

// Быстрый параметр T22: 6 бит, собирается из двух слов
// first_word, second_word – два последовательных 12-битных слова
int orbita_fast_t22_value(uint16_t first_word, uint16_t second_word);

// Быстрый параметр T24: 6 бит, аналогично T22
int orbita_fast_t24_value(uint16_t first_word, uint16_t second_word);

// Температурный код T11: 8 бит (0..255)
int orbita_temperature_code(uint16_t word);

// Преобразование кода температуры в градусы Цельсия (линейное)
double orbita_temperature_code_to_celsius(int code, double offset, double scale);

// Преобразование БУС (T25): сборка 16-битного значения из двух 12-битных слов
uint16_t orbita_bus_value(uint16_t high_word, uint16_t low_word);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_VALUE_CONVERTER_H
