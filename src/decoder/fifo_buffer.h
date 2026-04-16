/**
 * @file fifo_buffer.hpp
 * @brief Кольцевой буфер для хранения битов (0/1) телеметрического потока.
 *
 * Размер буфера задаётся константой FIFO_SIZE (2 500 000, как в оригинале).
 * Поддерживает добавление бита, чтение бита, чтение со смещением, сдвиг указателей.
 */

#ifndef ORBITA_FIFO_BUFFER_HPP
#define ORBITA_FIFO_BUFFER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace orbita {

class FifoBuffer {
public:
    static constexpr size_t FIFO_SIZE = 2500000;

    FifoBuffer();
    ~FifoBuffer() = default;

    // Запрет копирования
    FifoBuffer(const FifoBuffer&) = delete;
    FifoBuffer& operator=(const FifoBuffer&) = delete;

    // Разрешение перемещения
    FifoBuffer(FifoBuffer&&) = default;
    FifoBuffer& operator=(FifoBuffer&&) = default;

    // Добавить бит (0 или 1)
    void push(uint8_t bit);

    // Прочитать текущий бит и сдвинуть указатель чтения
    uint8_t pop();

    // Прочитать бит со смещением (без сдвига) offset = 0..(available-1)
    uint8_t peek(size_t offset) const;

    // Прочитать бит с абсолютным смещением от начала буфера (по модулю размера)
    uint8_t peekAbsolute(size_t pos) const;

    // Сдвинуть указатель чтения вперёд на n битов
    void skip(size_t n);

    // Сдвинуть указатель чтения назад на n битов
    void rewind(size_t n);

    // Получить количество доступных битов
    size_t available() const;

    // Проверить, пуст ли буфер
    bool empty() const;

    // Проверить, полон ли буфер
    bool full() const;

    // Сброс буфера (очистка)
    void reset();

private:
    std::vector<uint8_t> data_;
    size_t write_pos_;
    size_t read_pos_;
    size_t count_;
};

} // namespace orbita

#endif // ORBITA_FIFO_BUFFER_HPP
