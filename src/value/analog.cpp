#include "value_converter.h"

double orbita_analog_10bit_to_voltage(uint16_t word, double scale) {
    // Берём младшие 10 бит (T01)
    uint16_t raw = word & 0x3FF; // 0x3FF = 1023
    return (double)raw * scale;
}

double orbita_analog_9bit_to_voltage(uint16_t word, double scale) {
    // T01-01: младшие 9 бит
    uint16_t raw = word & 0x1FF; // 0x1FF = 511
    return (double)raw * scale;
}
