#include "fifo_buffer.h"
#include <algorithm>
#include <cstring>

namespace orbita {

FifoBuffer::FifoBuffer()
    : data_(FIFO_SIZE, 0)
    , write_pos_(0)
    , read_pos_(0)
    , count_(0)
{}

void FifoBuffer::push(uint8_t bit) {
    data_[write_pos_] = bit ? 1 : 0;
    write_pos_ = (write_pos_ + 1) % FIFO_SIZE;
    if (count_ < FIFO_SIZE) {
        ++count_;
    } else {
        // Переполнение – сдвигаем указатель чтения (старые данные теряются)
        read_pos_ = (read_pos_ + 1) % FIFO_SIZE;
    }
}

uint8_t FifoBuffer::pop() {
    if (count_ == 0) return 0;
    uint8_t bit = data_[read_pos_];
    read_pos_ = (read_pos_ + 1) % FIFO_SIZE;
    --count_;
    return bit;
}

uint8_t FifoBuffer::peek(size_t offset) const {
    if (offset >= count_) return 0;
    size_t pos = (read_pos_ + offset) % FIFO_SIZE;
    return data_[pos];
}

uint8_t FifoBuffer::peekAbsolute(size_t pos) const {
    return data_[pos % FIFO_SIZE];
}

void FifoBuffer::skip(size_t n) {
    if (n >= count_) {
        read_pos_ = write_pos_;
        count_ = 0;
    } else {
        read_pos_ = (read_pos_ + n) % FIFO_SIZE;
        count_ -= n;
    }
}

void FifoBuffer::rewind(size_t n) {
    if (n >= count_) {
        read_pos_ = write_pos_;
        count_ = 0;
    } else {
        if (n <= read_pos_) {
            read_pos_ -= n;
        } else {
            read_pos_ = FIFO_SIZE - (n - read_pos_);
        }
        count_ += n;
    }
}

size_t FifoBuffer::available() const {
    return count_;
}

bool FifoBuffer::empty() const {
    return count_ == 0;
}

bool FifoBuffer::full() const {
    return count_ == FIFO_SIZE;
}

void FifoBuffer::reset() {
    read_pos_ = write_pos_;
    count_ = 0;
    // Очистка данных не требуется – при записи старые значения перезапишутся
}

} // namespace orbita
