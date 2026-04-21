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
    // Если portion_samples == 0, то берутся все samples
    void computeThreshold(const int16_t* samples, size_t count, size_t portion_samples = 0);

    // Принудительная установка порога
    void setThreshold(int threshold);

    // Обработать один отсчёт АЦП
    void processSample(int16_t sample);

    // Обработать массив отсчётов
    void processSamples(const int16_t* samples, size_t count);

    // Сбросить внутреннее состояние (счётчики, порог не сбрасывается)
    void reset();

    // Для отладки
    int getThreshold() const { return threshold_; }
    bool isThresholdComputed() const { return threshold_computed_; }
void flush();
private:
    void outputBit(uint8_t bit);
    static int bankersRound(long double value);
    static int calcOutStep(int count);

    FifoBuffer& fifo_;
    Polarity polarity_;
    int threshold_;
    bool threshold_computed_;
    int numUp_;     // количество последовательных отсчётов >= порога
    int numDown_;   // количество последовательных отсчётов < порога
};

} // namespace orbita

#endif
