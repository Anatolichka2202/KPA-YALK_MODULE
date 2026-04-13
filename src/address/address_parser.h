/**
 * @file address_parser.h
 * @brief Внутренний парсинг строки адреса (без обрезания комментариев).
 *
 * Используется address_manager для разбора строк из файла.
 */

#ifndef ORBITA_ADDRESS_PARSER_H
#define ORBITA_ADDRESS_PARSER_H

#include "orbita_address.h"

#ifdef __cplusplus
extern "C" {
#endif

// Разбор одной строки (без удаления комментариев – строка должна быть уже очищена)
// Возвращает 0 при успехе, иначе код ошибки.
int address_parse_line(const char* line, orbita_channel_desc_t* out_desc);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_ADDRESS_PARSER_H
