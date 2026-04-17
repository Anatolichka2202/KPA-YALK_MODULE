#ifndef ORBITA_BITSTREAM_RECOVERER_H
#define ORBITA_BITSTREAM_RECOVERER_H

#include <cstdint>
#include <vector>
#include <functional>

#ifdef DEBUG_RECOVERER
#include <iostream>
#endif

namespace orbita {

class BitstreamRecoverer {
public:
    struct Config {
        double sample_rate_hz;      // Частота дискретизации АЦП (Гц)
        double bit_rate_hz;         // Битовая скорость телеметрии (бит/с)
        bool invert = false;        // Инвертировать выходные биты
        size_t fifo_size = 2500000; // Размер кольцевого буфера битов
    };

    explicit BitstreamRecoverer(const Config& cfg);

    // Принять массив сырых отсчётов, восстановить биты и вызвать callback для каждого бита
    void processSamples(const int16_t* samples, size_t count,
                        std::function<void(uint8_t bit)> bit_callback);

    // Альтернативно: работа с внутренним FIFO
    size_t availableBits() const;
    uint8_t readBit();
    void readBits(uint8_t* buffer, size_t max_bits, size_t& bits_read);

    // Сброс состояния (порог остаётся вычисленным)
    void reset();

    // Принудительно установить порог (если нужно)
    void setThreshold(int threshold);

private:
    Config config_;
    std::vector<uint8_t> fifo_;
    size_t write_pos_ = 0;
    size_t read_pos_ = 0;
    size_t count_ = 0;

    int threshold_ = 0;
    bool threshold_computed_ = false;
    int up_counter_ = 0;      // счётчик отсчётов выше порога
    int down_counter_ = 0;    // счётчик отсчётов ниже порога

    double points_per_bit_;   // = sample_rate_hz / bit_rate_hz

    // Вспомогательные методы FIFO
    void pushBit(uint8_t bit);
    uint8_t popBit();
    size_t fifoCount() const { return count_; }

    // Вычисление порога по первым 10% отсчётов
    void computeThreshold(const int16_t* samples, size_t count);

    // Вставить count битов value (0 или 1) в FIFO
    void pushBits(uint8_t bit, int count);
};

} // namespace orbita

#endif // ORBITA_BITSTREAM_RECOVERER_H
