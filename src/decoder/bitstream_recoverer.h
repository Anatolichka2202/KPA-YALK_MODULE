
#ifndef ORBITA_BITSTREAM_RECOVERER_H
#define ORBITA_BITSTREAM_RECOVERER_H

#include <cstdint>
#include "fifo_buffer.h"

namespace orbita {

class BitstreamRecoverer {
public:
    enum Polarity { NORMAL = 0, INVERTED = 1 }; // NORMAL: high->0, low->1; INVERTED: high->1, low->0

    BitstreamRecoverer(FifoBuffer& output_fifo, Polarity polarity = NORMAL);
    ~BitstreamRecoverer() = default;

    // Вычисляет порог по массиву samples (размер count)
    void computeThreshold(const int16_t* samples, size_t count);

    // Принудительно установить порог
    void setThreshold(int threshold);

    // Обработать один отсчёт АЦП (сравнить с порогом и обновить счётчики)
    void processSample(int16_t sample);

    // Обработать массив отсчётов
    void processSamples(const int16_t* samples, size_t count);

    // Сбросить внутреннее состояние (счётчики, но порог сохраняется)
    void reset();

private:
    void outputBit(uint8_t bit); // записывает бит в FIFO

    FifoBuffer& fifo_;
    Polarity polarity_;
    int threshold_;
    bool threshold_computed_;

    int numUp_;     // количество последовательных отсчётов >= порога
    int numDown_;   // количество последовательных отсчётов < порога

    // Вспомогательная функция: вычисляет outStep по формуле round(count / (10 / 3.145728))
    static int calcOutStep(int count);
};

} // namespace orbita

#endif
