#include "tlm_writer.h"
#include "../orbita_core/logger.h"
#include <cstring>
#include <chrono>
#include <filesystem>

namespace orbita {

static const int HEADER_SIZE = 256;

TlmWriter::TlmWriter(const std::string& filename, int informativnost)
    : informativnost_(informativnost)
{
    file_.open(std::filesystem::u8path(filename), std::ios::binary | std::ios::out | std::ios::trunc);
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

    // Заголовок обновляется после каждой порции. У M8 префикс блока
    // занимает 32 байта по WriteTLMBlockM08_04_02_01, у M16 — 24.
    if (currentBlock_ != blockNum) {
        currentBlock_ = blockNum;
        currentWords_ = 0;
        headerPosition_ = file_.tellp();
        char blockHeader[32] = {};
        std::memcpy(blockHeader, &blockNum, 4);
        std::memcpy(blockHeader + 8, &elapsedMs, 4);
        if (informativnost_ == 8) {
            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            if (const auto* calendar = std::localtime(&time)) {
                blockHeader[20] = static_cast<char>(calendar->tm_hour);
                blockHeader[21] = static_cast<char>(calendar->tm_min);
                blockHeader[22] = static_cast<char>(calendar->tm_sec);
            }
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count() % 1000;
            const auto milliseconds = static_cast<uint16_t>(ms);
            std::memcpy(blockHeader + 23, &milliseconds, sizeof(milliseconds));
        }
        file_.write(blockHeader, informativnost_ == 8 ? 32 : 24);
    }
    const auto wordCount = static_cast<uint32_t>(data.size());
    file_.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(wordCount * sizeof(uint16_t)));
    currentWords_ += wordCount;
    const auto end = file_.tellp();
    file_.seekp(headerPosition_ + std::streamoff(4));
    file_.write(reinterpret_cast<const char*>(&currentWords_), sizeof(currentWords_));
    // Время блока относится к его последней записанной порции, как и раньше
    // относилось к моменту завершения полного цикла.
    file_.write(reinterpret_cast<const char*>(&elapsedMs), sizeof(elapsedMs));
    file_.seekp(end);
    file_.flush();
}

bool TlmWriter::finish() {
    file_.flush();
    const bool ok = file_.good();
    file_.close();
    return ok && !file_.fail();
}

} // namespace orbita
