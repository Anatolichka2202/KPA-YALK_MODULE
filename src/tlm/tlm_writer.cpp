#include "tlm_writer.h"
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <iomanip>
#include <sstream>

namespace orbita {

static size_t getWordsPerBlock(int informativnost) {
    switch (informativnost) {
    case 16: return 65536;
    case 8:  return 32768;
    case 4:  return 16384;
    case 2:  return 8192;
    case 1:  return 4096;
    default: throw std::runtime_error("Invalid informativnost");
    }
}



TlmWriter::TlmWriter(const std::string& filename, int informativnost)
    : filename_(filename)
    , informativnost_(informativnost)
    , words_per_block_(getWordsPerBlock(informativnost))
    , bytes_written_(0)
{
    file_.open(filename, std::ios::binary | std::ios::trunc);
    if (!file_.is_open()) {
        throw std::runtime_error("Cannot open file for writing: " + filename);
    }
    writeHeader();
}

void TlmWriter::writeBytes(const void* data, size_t size) {
    file_.write(static_cast<const char*>(data), size);
    if (!file_.good()) {
        throw std::runtime_error("Write error");
    }
    bytes_written_ += size;
}

void TlmWriter::writeUint32(uint32_t value) {
    // little-endian: младший байт первый
    uint8_t buf[4];
    buf[0] = static_cast<uint8_t>(value);
    buf[1] = static_cast<uint8_t>(value >> 8);
    buf[2] = static_cast<uint8_t>(value >> 16);
    buf[3] = static_cast<uint8_t>(value >> 24);
    writeBytes(buf, 4);
}

void TlmWriter::writeUint16(uint16_t value) {
    uint8_t buf[2];
    buf[0] = static_cast<uint8_t>(value);
    buf[1] = static_cast<uint8_t>(value >> 8);
    writeBytes(buf, 2);
}

void TlmWriter::writeUint8(uint8_t value) {
    writeBytes(&value, 1);
}

void TlmWriter::writeHeader() {
    // Формируем строку заголовка по образцу Delphi:
    // "Complex=MERATMS-M.SYSTEM=ORBITAIV.DATA=yyyy:mm:dd.TIME=hh:mm:sszzz.INF=16........"
    // Дополняем нулями до 256 байт.
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);
    char time_buf[64];
    std::strftime(time_buf, sizeof(time_buf), "%Y:%m:%d", tm);
    std::string data_str = time_buf;
    std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm);
    std::string time_str = time_buf;

    std::ostringstream oss;
    oss << "Complex=MERATMS-M.SYSTEM=ORBITAIV.DATA=" << data_str
        << ".TIME=" << time_str << ".INF=" << informativnost_ << ".";
    std::string header_str = oss.str();
    if (header_str.size() > 256) header_str.resize(256);
    // Дополняем нулями до 256
    header_str.resize(256, '\0');

    writeBytes(header_str.c_str(), 256);
}

void TlmWriter::writeBlock(uint32_t block_num, uint32_t time_ms, const std::vector<uint16_t>& words) {
    if (words.size() != words_per_block_) {
        throw std::runtime_error("Invalid words count for block");
    }

    // Префикс (32 байта)
    writeUint32(block_num);
    writeUint32(static_cast<uint32_t>(words.size()));
    writeUint32(time_ms);
    writeUint32(0);                     // rez
    writeUint32(0xFFFFFFFF);            // prKPSEV

    // Вычисление часов, минут, секунд из time_ms
    uint32_t seconds = time_ms / 1000;
    uint8_t hour = (seconds / 3600) % 24;
    uint8_t minute = (seconds / 60) % 60;
    uint8_t sec = seconds % 60;
    uint16_t msec = time_ms % 1000;

    writeUint8(hour);
    writeUint8(minute);
    writeUint8(sec);
    writeUint16(msec);
    writeUint8(0);      // rez1
    writeUint8(0);      // prSEV
    writeUint8(0);      // error
    writeUint32(0);     // rez2

    // Данные
    for (uint16_t w : words) {
        writeUint16(w);
    }
}

} // namespace orbita
