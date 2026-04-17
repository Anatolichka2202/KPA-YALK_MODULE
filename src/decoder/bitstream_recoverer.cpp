#include "bitstream_recoverer.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>



namespace orbita {

BitstreamRecoverer::BitstreamRecoverer(const Config& cfg)
    : config_(cfg)
    , fifo_(cfg.fifo_size, 0)
{
    if (cfg.sample_rate_hz <= 0 || cfg.bit_rate_hz <= 0)
        throw std::invalid_argument("Invalid sample or bit rate");
    points_per_bit_ = cfg.sample_rate_hz / cfg.bit_rate_hz;
}

void BitstreamRecoverer::computeThreshold(const int16_t* samples, size_t count) {
    if (count == 0) return;
    size_t n = std::max(size_t(1), count / 10); // первые 10%
    int16_t min_val = samples[0];
    int16_t max_val = samples[0];
    for (size_t i = 1; i < n; ++i) {
        if (samples[i] < min_val) min_val = samples[i];
        if (samples[i] > max_val) max_val = samples[i];
    }
    threshold_ = (static_cast<int>(min_val) + static_cast<int>(max_val)) / 2;
    threshold_computed_ = true;

#ifdef DEBUG_RECOVERER
    std::cout << "Threshold computed: " << threshold_
              << " (min=" << min_val << ", max=" << max_val << ")" << std::endl;
#endif

}

void BitstreamRecoverer::pushBit(uint8_t bit) {
    fifo_[write_pos_] = bit ? 1 : 0;
    write_pos_ = (write_pos_ + 1) % fifo_.size();
    if (count_ < fifo_.size()) {
        ++count_;
    } else {
        // Переполнение – сдвигаем чтение (теряем старые данные)
        read_pos_ = (read_pos_ + 1) % fifo_.size();
    }
}

uint8_t BitstreamRecoverer::popBit() {
    if (count_ == 0) return 0;
    uint8_t bit = fifo_[read_pos_];
    read_pos_ = (read_pos_ + 1) % fifo_.size();
    --count_;
    return bit;
}

void BitstreamRecoverer::pushBits(uint8_t bit, int count) {
    for (int i = 0; i < count; ++i) {
        pushBit(bit);
    }
}

void BitstreamRecoverer::processSamples(const int16_t* samples, size_t count,
                                        std::function<void(uint8_t)> bit_callback) {

#ifdef DEBUG_RECOVERER
    static int call_count = 0;
    if (++call_count <= 5) {
        std::cout << "processSamples call " << call_count << ", count=" << count << std::endl;
    }
#endif

    if (!threshold_computed_) {
        computeThreshold(samples, count);
    }

    for (size_t i = 0; i < count; ++i) {
        bool above = (samples[i] >= threshold_);
        if (above) {
            ++up_counter_;
            if (down_counter_ > 0) {
                int out_step = std::max(1, (int)std::round(down_counter_ / points_per_bit_));
                // below -> прямой:1, инверсный:0
                uint8_t out_bit = config_.invert ? 0 : 1;

#ifdef DEBUG_RECOVERER
                static int trans_count = 0;
                if (trans_count++ < 10) {
                    std::cout << "Transition below->above: down_counter=" << down_counter_ << ", out_step=" << out_step << ", out_bit=" << (int)out_bit << std::endl;
                }
#endif

                pushBits(out_bit, out_step);
                if (bit_callback) {
                    for (int j = 0; j < out_step; ++j) bit_callback(out_bit);
                }
                down_counter_ = 0;
            }
        } else {
            ++down_counter_;
            if (up_counter_ > 0) {
                int out_step = std::max(1, (int)std::round(up_counter_ / points_per_bit_));
                // above -> прямой:0, инверсный:1
                uint8_t out_bit = config_.invert ? 1 : 0;
                pushBits(out_bit, out_step);
                if (bit_callback) {
                    for (int j = 0; j < out_step; ++j) bit_callback(out_bit);
                }
                up_counter_ = 0;
            }
        }
    }
}

size_t BitstreamRecoverer::availableBits() const {
    return count_;
}

uint8_t BitstreamRecoverer::readBit() {
    return popBit();
}

void BitstreamRecoverer::readBits(uint8_t* buffer, size_t max_bits, size_t& bits_read) {
    bits_read = 0;
    while (bits_read < max_bits && count_ > 0) {
        buffer[bits_read++] = popBit();
    }
}

void BitstreamRecoverer::reset() {
    up_counter_ = 0;
    down_counter_ = 0;
    // Порог сохраняется, FIFO сбрасывать не обязательно? Лучше сбросить указатели чтения/записи.
    write_pos_ = 0;
    read_pos_ = 0;
    count_ = 0;
    // threshold_computed_ остаётся true, чтобы не пересчитывать заново
}

void BitstreamRecoverer::setThreshold(int threshold) {
    threshold_ = threshold;
    threshold_computed_ = true;
}

} // namespace orbita
