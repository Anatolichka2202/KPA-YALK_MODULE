/**
 * @file tlm_reader.hpp
 * @brief Чтение телеметрических файлов формата TLM.
 */

#ifndef ORBITA_TLM_READER_HPP
#define ORBITA_TLM_READER_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>

namespace orbita {

class TlmReader {
public:
    /**
     * @brief Открывает TLM-файл для чтения.
     * @param filename путь к файлу
     * @throws std::runtime_error при ошибке открытия или неверном заголовке
     */
    explicit TlmReader(const std::string& filename);
    ~TlmReader() = default;

    // Запрет копирования
    TlmReader(const TlmReader&) = delete;
    TlmReader& operator=(const TlmReader&) = delete;

    // Разрешение перемещения
    TlmReader(TlmReader&&) = default;
    TlmReader& operator=(TlmReader&&) = default;

    /**
     * @brief Возвращает заголовок (256 байт) в виде строки.
     */
    std::string getHeader() const { return header_; }

    /**
     * @brief Возвращает информативность, извлечённую из заголовка.
     */
    int getInformativnost() const { return informativnost_; }

    /**
     * @brief Перемещает указатель чтения к первому блоку (после заголовка).
     */
    void seekToFirstBlock();

    /**
     * @brief Читает следующий блок.
     * @param block_num   [out] номер блока (1..)
     * @param time_ms     [out] время в миллисекундах
     * @param words       [out] вектор слов (размер будет установлен)
     * @return true если блок успешно прочитан, false если достигнут конец файла
     * @throws std::runtime_error при ошибке чтения или несоответствии размера
     */
    bool readNextBlock(uint32_t& block_num, uint32_t& time_ms, std::vector<uint16_t>& words);

    /**
     * @brief Возвращает общее количество блоков в файле (требует перемотки).
     */
    size_t getBlockCount();

private:
    std::string filename_;
    std::ifstream file_;
    std::string header_;
    int informativnost_;
    size_t words_per_block_;
    size_t block_data_size_;   // размер блока данных в байтах (префикс + слова)

    void readHeader();
    uint32_t readUint32();
    uint16_t readUint16();
    uint8_t readUint8();
    void readBytes(void* data, size_t size);
};

} // namespace orbita

#endif // ORBITA_TLM_READER_HPP
