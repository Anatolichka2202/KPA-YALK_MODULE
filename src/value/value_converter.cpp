#include "value_converter.h"

namespace orbita {

double analog10bitToVoltage(uint16_t word, double scale) {
    uint16_t value = word & 0x3FF; // 10 бит
    return static_cast<double>(value) * scale;
}

double analog9bitToVoltage(uint16_t word, double scale) {
    uint16_t value = word & 0x1FF; // 9 бит
    return static_cast<double>(value) * scale;
}

int contactExtractBit(uint16_t word, int bitNum) {
    if (bitNum < 1 || bitNum > 10) return 0;
    // В Delphi-коде: значение сначала сдвигается вправо на 1 (word shr 1),
    // затем биты нумеруются: 1 -> сдвиг 9, 2 -> 8, ..., 10 -> 0.
    uint16_t shifted = word >> 1;
    int shift = 10 - bitNum; // для bitNum=1 shift=9, для bitNum=10 shift=0
    return (shifted >> shift) & 1;
}

int fastT21Value(uint16_t word) {
    // T21: 8 бит (0..255), биты 3..10
    return (word >> 3) & 0xFF;
}

int fastT22Value(uint16_t first_word, uint16_t second_word) {
    // Реализация из Delphi (BuildFastValueT22)
    uint16_t fastValBuf = 0;
    // Обработка первого слова
    if (first_word & 0x400) fastValBuf = (fastValBuf << 1) | 1;
    else fastValBuf <<= 1;
    if (first_word & 0x200) fastValBuf = (fastValBuf << 1) | 1;
    else fastValBuf <<= 1;
    if (first_word & 0x100) fastValBuf = (fastValBuf << 1) | 1;
    else fastValBuf <<= 1;
    if (first_word & 0x080) fastValBuf = (fastValBuf << 1) | 1;
    else fastValBuf <<= 1;
    if (first_word & 0x040) fastValBuf = (fastValBuf << 1) | 1;
    else fastValBuf <<= 1;
    if (first_word & 0x004) fastValBuf = (fastValBuf << 1) | 1;
    else fastValBuf <<= 1;

    // Обработка второго слова
    uint16_t temp = (second_word >> 10) & 0x07; // 3 бита
    fastValBuf = (fastValBuf << 3) | temp;
    if (second_word & 0x002) fastValBuf = (fastValBuf << 1) | 1;
    else fastValBuf <<= 1;
    if (second_word & 0x001) fastValBuf = (fastValBuf << 1) | 1;
    else fastValBuf <<= 1;
    if (second_word & 0x800) fastValBuf = (fastValBuf << 1) | 1;
    else fastValBuf <<= 1;

    return static_cast<int>(fastValBuf);
}

int fastT24Value(uint16_t first_word, uint16_t second_word) {
    // Реализация из Delphi (BuildFastValueT24)
    uint16_t fastValBuf = 0;
    // Собираем первое слово (младшие 6 бит)
    fastValBuf = first_word & 0x3F;
    // Второе слово – старшие 6 бит
    uint16_t high = (second_word >> 6) & 0x3F;
    fastValBuf = (high << 6) | fastValBuf;
    return static_cast<int>(fastValBuf);
}

int temperatureCode(uint16_t word) {
    // В Delphi: masGroup[nPoint] shr 1 (8 бит)
    return (word >> 1) & 0xFF;
}

double temperatureCodeToCelsius(int code, double offset, double scale) {
    return offset + static_cast<double>(code) * scale;
}

uint16_t busValue(uint16_t high_word, uint16_t low_word) {
    // По Delphi: BuildBusValue
    // Отбрасываем младшие 3 бита из каждого слова (т.е. берём биты 3..11)
    uint16_t bufH = (high_word & 0x0FF8) >> 3;
    uint16_t bufL = (low_word  & 0x0FF8) >> 3;
    return (bufH << 8) | bufL;
}

} // namespace orbita
