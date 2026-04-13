#include "fifo_buffer.h"
#include <string.h>

void orbita_fifo_init(orbita_fifo_t* fifo) {
    memset(fifo->data, 0, FIFO_SIZE);
    fifo->write_pos = 0;
    fifo->read_pos = 0;
    fifo->count = 0;
}

void orbita_fifo_push(orbita_fifo_t* fifo, uint8_t bit) {
    fifo->data[fifo->write_pos] = bit ? 1 : 0;
    fifo->write_pos = (fifo->write_pos + 1) % FIFO_SIZE;
    if (fifo->count < FIFO_SIZE) fifo->count++;
    else {
        // Переполнение – сдвигаем указатель чтения
        fifo->read_pos = (fifo->read_pos + 1) % FIFO_SIZE;
    }
}

uint8_t orbita_fifo_pop(orbita_fifo_t* fifo) {
    if (fifo->count == 0) return 0;
    uint8_t bit = fifo->data[fifo->read_pos];
    fifo->read_pos = (fifo->read_pos + 1) % FIFO_SIZE;
    fifo->count--;
    return bit;
}

uint8_t orbita_fifo_peek(const orbita_fifo_t* fifo, size_t offset) {
    if (offset >= fifo->count) return 0;
    size_t pos = (fifo->read_pos + offset) % FIFO_SIZE;
    return fifo->data[pos];
}

uint8_t orbita_fifo_peek_absolute(const orbita_fifo_t* fifo, size_t pos) {
    return fifo->data[pos % FIFO_SIZE];
}

void orbita_fifo_skip(orbita_fifo_t* fifo, size_t n) {
    if (n >= fifo->count) {
        fifo->read_pos = fifo->write_pos;
        fifo->count = 0;
    } else {
        fifo->read_pos = (fifo->read_pos + n) % FIFO_SIZE;
        fifo->count -= n;
    }
}

void orbita_fifo_rewind(orbita_fifo_t* fifo, size_t n) {
    if (n >= fifo->count) {
        fifo->read_pos = fifo->write_pos;
        fifo->count = 0;
    } else {
        if (n <= fifo->read_pos) fifo->read_pos -= n;
        else fifo->read_pos = FIFO_SIZE - (n - fifo->read_pos);
        fifo->count += n;
    }
}

size_t orbita_fifo_available(const orbita_fifo_t* fifo) {
    return fifo->count;
}

void orbita_fifo_reset(orbita_fifo_t* fifo) {
    fifo->read_pos = fifo->write_pos;
    fifo->count = 0;
}
