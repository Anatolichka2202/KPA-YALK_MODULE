// bitstream_recoverer.h
#ifndef ORBITA_BITSTREAM_RECOVERER_H
#define ORBITA_BITSTREAM_RECOVERER_H

#include <cstdint>
#include "fifo_buffer.h"

namespace orbita {

class BitstreamRecoverer {
public:
    enum Polarity { NORMAL = 0, INVERTED = 1 };

    BitstreamRecoverer(FifoBuffer& output_fifo, Polarity polarity = NORMAL);
    ~BitstreamRecoverer() = default;

    // Вычисляет порог по первым portion_samples отсчётам из samples
    void computeThreshold(const int16_t* samples, size_t count, size_t portion_samples = 0);
    // Принудительная установка порога
    void setThreshold(int threshold);
    // Обработать один отсчёт АЦП
    void processSample(int16_t sample);
    // Обработать массив отсчётов
    void processSamples(const int16_t* samples, size_t count);
    // Сбросить внутреннее состояние (счётчики)
    void reset();
    // Вытолкнуть оставшиеся серии (вызвать при останове)
    void flush();

    // Для отладки
    int getThreshold() const { return threshold_; }
    bool isThresholdComputed() const { return threshold_computed_; }

        static int calcOutStep(int count);
private:
    void outputBit(uint8_t bit);

    FifoBuffer& fifo_;
    Polarity polarity_;
    int threshold_;
    bool threshold_computed_;
    int numUp_;     // количество последовательных отсчётов >= порога
    int numDown_;   // количество последовательных отсчётов < порога

    // Для отложенного вычисления порога
    size_t skip_samples_before_threshold_ = 0; // сколько отсчётов пропустить
    size_t skipped_ = 0;
    std::vector<int16_t> threshold_buffer_; // буфер для накопления данных для порога
    size_t threshold_sample_count_ = 0;
};

} // namespace orbita

#endif