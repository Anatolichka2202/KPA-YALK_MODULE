/**
 * @file fifo_buffer.h
 * @brief Кольцевой буфер для хранения битов (0/1) телеметрического потока.
 *
 * Размер буфера задаётся константой FIFO_SIZE (2 500 000, как в оригинале).
 * Поддерживает добавление бита, чтение бита, чтение со смещением, сдвиг указателей.
 */

#ifndef ORBITA_FIFO_BUFFER_H
#define ORBITA_FIFO_BUFFER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIFO_SIZE 2500000

typedef struct {
    uint8_t data[FIFO_SIZE];  // храним биты как 0/1 (можно использовать uint8_t)
    size_t write_pos;         // позиция записи (0..FIFO_SIZE-1)
    size_t read_pos;          // позиция чтения
    size_t count;             // количество накопленных битов
} orbita_fifo_t;

// Инициализация буфера (обнуление)
void orbita_fifo_init(orbita_fifo_t* fifo);

// Добавить бит (0 или 1)
void orbita_fifo_push(orbita_fifo_t* fifo, uint8_t bit);

// Прочитать текущий бит и сдвинуть указатель чтения
uint8_t orbita_fifo_pop(orbita_fifo_t* fifo);

// Прочитать бит со смещением (без сдвига) offset = 0..(count-1)
uint8_t orbita_fifo_peek(const orbita_fifo_t* fifo, size_t offset);

// Прочитать бит с абсолютным смещением от начала буфера (по модулю размера)
uint8_t orbita_fifo_peek_absolute(const orbita_fifo_t* fifo, size_t pos);

// Сдвинуть указатель чтения вперёд на n битов
void orbita_fifo_skip(orbita_fifo_t* fifo, size_t n);

// Сдвинуть указатель чтения назад на n битов
void orbita_fifo_rewind(orbita_fifo_t* fifo, size_t n);

// Получить количество доступных битов
size_t orbita_fifo_available(const orbita_fifo_t* fifo);

// Сброс буфера (очистка)
void orbita_fifo_reset(orbita_fifo_t* fifo);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_FIFO_BUFFER_H
