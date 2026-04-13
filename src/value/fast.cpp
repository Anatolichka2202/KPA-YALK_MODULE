#include "value_converter.h"

int orbita_fast_t21_value(uint16_t word) {
    // T21: 8 бит (биты 3..10? В Delphi: masGroup[nPoint] shr 3)
    return (word >> 3) & 0xFF;
}

int orbita_fast_t22_value(uint16_t first_word, uint16_t second_word) {
    // T22: 6 бит. В Delphi BuildFastValueT22.
    // Алгоритм: первое слово даёт 6 бит (биты 12..7?), второе – 6 бит.
    // Упрощённо (по коду Delphi) – результат в диапазоне 0..63.
    int fastVal = 0;
    // Первое слово: биты 12,11,10,9,8,7? (по коду сложно, но мы возьмём младшие 6 бит)
    fastVal = (first_word >> 6) & 0x3F; // Пример, нужно уточнить.
    return fastVal;
}

int orbita_fast_t24_value(uint16_t first_word, uint16_t second_word) {
    // T24: 6 бит, аналогично T22.
    return orbita_fast_t22_value(first_word, second_word); // временно
}
