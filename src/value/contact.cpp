#include "value_converter.h"

int orbita_contact_extract_bit(uint16_t word, int bitNum) {
    // В Delphi: value shr 1 (отбрасываем 12-й бит), затем сдвиг.
    // Нумерация битов: 1 – старший (9-й бит после сдвига), 10 – младший (0-й).
    uint16_t shifted = word >> 1; // теперь биты 11..0 стали 10..0
    int bitIndex = 10 - bitNum;   // пример: bitNum=1 -> индекс 9, bitNum=10 -> индекс 0
    if (bitIndex < 0 || bitIndex > 9) return 0;
    return (shifted >> bitIndex) & 1;
}
