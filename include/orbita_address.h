/**
 * @file orbita_address.h
 * @brief Парсинг адресов телеметрии "Орбита-IV".
 *
 * Преобразует строки вида "M16П2A10B21C10D11T21" в структуру orbita_channel_desc_t.
 *
 * Типы адресов (orbita_addr_type_t):
 * - ORBITA_ADDR_TYPE_ANALOG_10BIT  (T01)   – 10 бит, 0..1023
 * - ORBITA_ADDR_TYPE_ANALOG_9BIT   (T01-01) – 9 бит, 0..511
 * - ORBITA_ADDR_TYPE_CONTACT       (T05)   – один бит (1..10)
 * - ORBITA_ADDR_TYPE_FAST_1        (T21)   – 8 бит, 0..255
 * - ORBITA_ADDR_TYPE_FAST_2        (T22)   – 6 бит, 0..63
 * - ORBITA_ADDR_TYPE_FAST_3        (T23)   – (не используется)
 * - ORBITA_ADDR_TYPE_FAST_4        (T24)   – 6 бит, 0..63
 * - ORBITA_ADDR_TYPE_BUS           (T25)   – 16 бит (сборка из двух слов)
 * - ORBITA_ADDR_TYPE_TEMPERATURE   (T11)   – 8 бит код температуры
 *
 * Структура orbita_channel_desc_t:
 * - numOutElemG – смещение в группе (1‑индексированное)
 * - stepOutG    – шаг между выборками
 * - adressType  – тип адреса (см. выше)
 * - bitNumber   – для контактных: номер бита 1..10
 * - flagGroup   – 1, если выборка из конкретных групп (arrNumGroup)
 * - flagCikl    – 1, если выборка из конкретных циклов (arrNumCikl)
 * - arrNumGroup, numGroups – массив номеров групп (1..32)
 * - arrNumCikl, numCikls  – массив номеров циклов (1..4)
 *
 * Функции:
 * - orbita_parse_address_line – разбор одной строки
 * - orbita_load_address_file  – загрузка файла с адресами (обрезает комментарии)
 * - orbita_free_* – освобождение памяти
 */

#ifndef ORBITA_ADDRESS_H
#define ORBITA_ADDRESS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ORBITA_ADDR_TYPE_ANALOG_10BIT = 0,
    ORBITA_ADDR_TYPE_CONTACT = 1,
    ORBITA_ADDR_TYPE_FAST_2 = 2,
    ORBITA_ADDR_TYPE_FAST_1 = 3,
    ORBITA_ADDR_TYPE_FAST_3 = 4,
    ORBITA_ADDR_TYPE_FAST_4 = 5,
    ORBITA_ADDR_TYPE_BUS = 6,
    ORBITA_ADDR_TYPE_TEMPERATURE = 7,
    ORBITA_ADDR_TYPE_ANALOG_9BIT = 8
} orbita_addr_type_t;

typedef struct {
    uint32_t numOutElemG;
    uint32_t stepOutG;
    uint8_t  adressType;
    uint8_t  bitNumber;
    uint8_t  flagGroup;
    uint8_t  flagCikl;
    uint16_t* arrNumGroup;
    uint16_t  numGroups;
    uint16_t* arrNumCikl;
    uint16_t  numCikls;
} orbita_channel_desc_t;

// Парсинг одной строки
int orbita_parse_address_line(const char* line, orbita_channel_desc_t* out_desc);

void orbita_free_channel_desc(orbita_channel_desc_t* desc);

// Загрузка из файла (автоматически обрезает комментарии после табуляции/пробела)
int orbita_load_address_file(const char* filename, orbita_channel_desc_t** out_channels, int* out_count);

void orbita_free_channels(orbita_channel_desc_t* channels, int count);

#ifdef __cplusplus
}
#endif

#endif
