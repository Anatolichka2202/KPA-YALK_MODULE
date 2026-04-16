#include "tlm_reader.h"
#include <cstring>
#include <stdexcept>
#include <cctype>

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

static int parseInformativnostFromHeader(const std::string& header) {
    // Ищем "INF="
    size_t pos = header.find("INF=");
    if (pos == std::string::npos) return 0;
    pos += 4;
    int inf = 0;
    while (pos < header.size() && std::isdigit(static_cast<unsigned char>(header[pos]))) {
        inf = inf * 10 + (header[pos] - '0');
        ++pos;
    }
    return inf;
}

TlmReader::TlmReader(const std::string& filename)
    : filename_(filename)
    , informativnost_(0)
    , words_per_block_(0)
    , block_data_size_(0)
{
    file_.open(filename, std::ios::binary);
    if (!file_.is_open()) {
        throw std::runtime_error("Cannot open file for reading: " + filename);
    }
    readHeader();
    informativnost_ = parseInformativnostFromHeader(header_);
    if (informativnost_ == 0) {
        throw std::runtime_error("Cannot find INF= in header");
    }
    words_per_block_ = getWordsPerBlock(informativnost_);
    block_data_size_ = 32 + words_per_block_ * 2; // префикс 32 + слова
}

void TlmReader::readBytes(void* data, size_t size) {
    file_.read(static_cast<char*>(data), size);
    if (!file_.good() && !file_.eof()) {
        throw std::runtime_error("Read error");
    }
}

uint8_t TlmReader::readUint8() {
    uint8_t val;
    readBytes(&val, 1);
    return val;
}

uint16_t TlmReader::readUint16() {
    uint8_t buf[2];
    readBytes(buf, 2);
    return static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
}

uint32_t TlmReader::readUint32() {
    uint8_t buf[4];
    readBytes(buf, 4);
    return static_cast<uint32_t>(buf[0]) |
           (static_cast<uint32_t>(buf[1]) << 8) |
           (static_cast<uint32_t>(buf[2]) << 16) |
           (static_cast<uint32_t>(buf[3]) << 24);
}

void TlmReader::readHeader() {
    header_.resize(256);
    file_.read(&header_[0], 256);
    if (!file_.good()) {
        throw std::runtime_error("Cannot read header");
    }
    // Удаляем завершающие нули для удобства
    size_t end = header_.find('\0');
    if (end != std::string::npos) header_.resize(end);
}

void TlmReader::seekToFirstBlock() {
    file_.clear();
    file_.seekg(256, std::ios::beg);
    if (!file_.good()) {
        throw std::runtime_error("Seek error");
    }
}

bool TlmReader::readNextBlock(uint32_t& block_num, uint32_t& time_ms, std::vector<uint16_t>& words) {
    // Проверяем, остались ли данные
    if (file_.eof()) return false;
    std::streampos pos = file_.tellg();
    if (pos == static_cast<std::streampos>(-1)) return false;
    // Проверяем, что осталось достаточно байт
    file_.seekg(0, std::ios::end);
    std::streamsize total = file_.tellg();
    file_.seekg(pos, std::ios::beg);
    if (total - pos < static_cast<std::streamsize>(block_data_size_)) {
        return false; // недостаточно данных для полного блока
    }

    // Читаем префикс
    uint32_t read_block_num = readUint32();
    uint32_t word_count = readUint32();
    uint32_t read_time_ms = readUint32();
    // Пропускаем rez (4 байта)
    readUint32();
    // Пропускаем prKPSEV (4 байта)
    readUint32();
    // Пропускаем hour, minute, sec (1+1+1=3 байта)
    readUint8(); readUint8(); readUint8();
    // Пропускаем mSec (2 байта)
    readUint16();
    // Пропускаем rez1 (1 байт)
    readUint8();
    // Пропускаем prSEV (1 байт)
    readUint8();
    // Пропускаем error (1 байт)
    readUint8();
    // Пропускаем rez2 (4 байта)
    readUint32();

    if (word_count != words_per_block_) {
        throw std::runtime_error("Block word count mismatch");
    }

    words.resize(word_count);
    for (size_t i = 0; i < word_count; ++i) {
        words[i] = readUint16();
    }

    block_num = read_block_num;
    time_ms = read_time_ms;
    return true;
}

size_t TlmReader::getBlockCount() {
    std::streampos old_pos = file_.tellg();
    seekToFirstBlock();
    size_t count = 0;
    uint32_t bn, tm;
    std::vector<uint16_t> words;
    while (readNextBlock(bn, tm, words)) {
        ++count;
    }
    file_.clear();
    file_.seekg(old_pos, std::ios::beg);
    return count;
}

} // namespace orbita
