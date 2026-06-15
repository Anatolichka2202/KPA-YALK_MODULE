#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <ctime>

namespace orbita {

class TlmWriter {
public:
    TlmWriter(const std::string& filename, int informativnost);
    ~TlmWriter();

    TlmWriter(const TlmWriter&) = delete;
    TlmWriter& operator=(const TlmWriter&) = delete;

    // Записать один цикл (65536 слов для M16)
    // blockNum  — номер блока (1, 2, ...)
    // elapsedMs — миллисекунд с начала записи
    // data      — 16-битные слова цикла
    void writeBlock(uint32_t blockNum, uint32_t elapsedMs,
                    const std::vector<uint16_t>& data);

    bool isOpen() const { return file_.is_open(); }

private:
    std::ofstream file_;
    int informativnost_;

    void writeHeader();
};

} // namespace orbita
