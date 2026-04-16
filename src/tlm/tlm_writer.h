/**
 * @file tlm_writer.hpp
 * @brief Запись телеметрических данных в файл формата TLM.
 *
 * Формат TLM:
 * - Заголовок: 256 байт, текстовая строка с ключами, дополненная нулями.
 * - Блок данных: префикс (32 байта) + массив 16-битных слов (little-endian).
 */

#ifndef ORBITA_TLM_WRITER_HPP
#define ORBITA_TLM_WRITER_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>

namespace orbita {

class TlmWriter {
public:
    /**
     * @brief Создаёт писатель и открывает файл для записи.
     * @param filename         путь к файлу
     * @param informativnost   информативность (1,2,4,8,16) – определяет размер блока
     * @throws std::runtime_error при ошибке открытия/записи заголовка
     */
    TlmWriter(const std::string& filename, int informativnost);
    ~TlmWriter() = default;

    // Запрет копирования
    TlmWriter(const TlmWriter&) = delete;
    TlmWriter& operator=(const TlmWriter&) = delete;

    // Разрешение перемещения
    TlmWriter(TlmWriter&&) = default;
    TlmWriter& operator=(TlmWriter&&) = default;

    /**
     * @brief Записывает один блок (цикл из 32 групп).
     * @param block_num   номер блока (начинается с 1)
     * @param time_ms     время в миллисекундах с начала записи (например, GetTickCount())
     * @param words       массив 16-битных слов (размер должен соответствовать masCircleSize)
     * @throws std::runtime_error при ошибке записи или неверном размере words
     */
    void writeBlock(uint32_t block_num, uint32_t time_ms, const std::vector<uint16_t>& words);

    /**
     * @brief Возвращает количество записанных байт в файл (полезно для индикации прогресса).
     */
    uint64_t bytesWritten() const { return bytes_written_; }

    /**
     * @brief Возвращает имя файла.
     */
    const std::string& filename() const { return filename_; }

    /**
     * @brief Возвращает информативность.
     */
    int informativnost() const { return informativnost_; }

private:
    std::string filename_;
    int informativnost_;
    size_t words_per_block_;   // masCircleSize
    std::ofstream file_;
    uint64_t bytes_written_;

    void writeHeader();
    void writeUint32(uint32_t value);
    void writeUint16(uint16_t value);
    void writeUint8(uint8_t value);
    void writeBytes(const void* data, size_t size);
};

} // namespace orbita

#endif // ORBITA_TLM_WRITER_HPP
