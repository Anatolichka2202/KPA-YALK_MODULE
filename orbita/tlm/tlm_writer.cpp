#include "tlm_writer.h"
#include "../orbita_core/logger.h"
#include <cstring>
#include <chrono>

namespace orbita {

static const int HEADER_SIZE = 256;

TlmWriter::TlmWriter(const std::string& filename, int informativnost)
    : informativnost_(informativnost)
{
    file_.open(filename, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
        LOG_ERROR("TlmWriter: cannot open file: %s", filename.c_str());
        return;
    }
    writeHeader();
    LOG_INFO("TlmWriter: recording to %s (INF=%d)", filename.c_str(), informativnost_);
}

TlmWriter::~TlmWriter() {
    if (file_.is_open()) file_.close();
}

void TlmWriter::writeHeader() {
    char header[HEADER_SIZE] = {};

    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);

    char buf[HEADER_SIZE];
    int n = std::snprintf(buf, sizeof(buf),
        "Complex=MERATMS-M.\r\n"
        "SYSTEM=ORBITAIV.\r\n"
        "DATA=%04d:%02d:%02d.\r\n"
        "TIME=%02d:%02d:%02d000.\r\n"
        "INF=%d.\r\n"
        "MODE=WORK.\r\n",
        tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
        tm->tm_hour, tm->tm_min, tm->tm_sec,
        informativnost_);

    if (n > 0 && n < HEADER_SIZE)
        std::memcpy(header, buf, static_cast<size_t>(n));

    file_.write(header, HEADER_SIZE);
}

void TlmWriter::writeBlock(uint32_t blockNum, uint32_t elapsedMs,
                           const std::vector<uint16_t>& data)
{
    if (!file_.is_open()) return;

    // Block header: blockNum(4) + wordCount(4) + timeMs(4) + reserved(12) = 24 bytes
    uint32_t wordCount = static_cast<uint32_t>(data.size());
    char blockHeader[24] = {};
    std::memcpy(blockHeader + 0,  &blockNum,  4);
    std::memcpy(blockHeader + 4,  &wordCount, 4);
    std::memcpy(blockHeader + 8,  &elapsedMs, 4);
    // bytes 12..23: reserved, already zero

    file_.write(blockHeader, sizeof(blockHeader));
    file_.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(wordCount * sizeof(uint16_t)));
}

} // namespace orbita
