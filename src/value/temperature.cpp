#include "value_converter.h"

int orbita_temperature_code(uint16_t word) {
    // T11: 8 бит (младшие 8 бит после сдвига на 1)
    return (word >> 1) & 0xFF;
}

double orbita_temperature_code_to_celsius(int code, double offset, double scale) {
    // Линейное преобразование: градусы = offset + code * scale
    return offset + (double)code * scale;
}
