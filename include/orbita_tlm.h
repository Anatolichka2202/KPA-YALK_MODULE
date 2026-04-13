/**
 * @file orbita_tlm.h
 * @brief Запись и чтение телеметрических файлов (.tlm).
 *
 * Формат TLM:
 * - Заголовок: 256 байт, текстовая строка с ключами, дополненная нулями.
 * - Блок данных: префикс (16 байт) + массив 16‑битных слов (little-endian).
 *
 * Запись: открыть файл, записать заголовок, затем последовательно блоки.
 * Чтение: открыть файл, пропустить заголовок, читать блоки.
 */

#ifndef ORBITA_TLM_H
#define ORBITA_TLM_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct orbita_tlm_writer orbita_tlm_writer_t;
typedef struct orbita_tlm_reader orbita_tlm_reader_t;

// === Запись ===
// Создать писатель, открыть файл. informativnost = 1,2,4,8,16.
orbita_tlm_writer_t* orbita_tlm_writer_open(const char* filename, int informativnost);
void orbita_tlm_writer_close(orbita_tlm_writer_t* w);

// Записать один блок (цикл из 32 групп)
// block_num – номер блока (1..)
// time_ms – время в миллисекундах с начала записи
// words – массив 16‑битных слов (размер = masCircleSize)
// word_count – количество слов
int orbita_tlm_writer_write_block(orbita_tlm_writer_t* w, uint32_t block_num, uint32_t time_ms, const uint16_t* words, size_t word_count);

// === Чтение ===
orbita_tlm_reader_t* orbita_tlm_reader_open(const char* filename);
void orbita_tlm_reader_close(orbita_tlm_reader_t* r);

// Пропустить заголовок (вызывать после открытия)
int orbita_tlm_reader_skip_header(orbita_tlm_reader_t* r);

// Прочитать следующий блок (выделяет память под words, нужно освободить free)
int orbita_tlm_reader_read_block(orbita_tlm_reader_t* r, uint32_t* block_num, uint32_t* time_ms, uint16_t** words, size_t* word_count);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_TLM_H
