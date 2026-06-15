#include <cfenv>
#include "bitstream_recover.h"
#include <cmath>
#include <algorithm>
#include <numbers>
#include "../orbita_core/logger.h"
#include <cmath>
namespace orbita {

static constexpr long double FREQ_RATIO = 10.0L / 3.145728L;

static int bankersRound(double value) {
    double intPart;
    double frac = std::modf(value, &intPart);
    if (frac < 0.5) return static_cast<int>(intPart);
    if (frac > 0.5) return static_cast<int>(intPart + 1.0);
    // frac == 0.5 → округление к чётному
    int intVal = static_cast<int>(intPart);
    return (intVal % 2 == 0) ? intVal : intVal + 1;
}

int BitstreamRecoverer::calcOutStep(int count) {
    if (count == 0) return 0;
    long double value = (long double)count / FREQ_RATIO;
    std::fesetround(FE_TONEAREST);
    return bankersRound(value);
}

BitstreamRecoverer::BitstreamRecoverer(FifoBuffer& output_fifo, Polarity polarity)
    : fifo_(output_fifo)
    , polarity_(polarity)
    , threshold_(0)
    , threshold_computed_(false)
    , numUp_(0)
    , numDown_(0)
    , skip_samples_before_threshold_(0) // примерно 0 мс задержки
    , skipped_(0)
{
    threshold_buffer_.reserve(1024 * 1024); // под буфер для вычисления порога
}

void BitstreamRecoverer::computeThreshold(const int16_t* samples, size_t count, size_t portion_samples) {
    if (count == 0) return;
    size_t effectiveCount = (portion_samples > 0 && portion_samples < count) ? portion_samples : count;
    int16_t minVal = samples[0];
    int16_t maxVal = samples[0];
    for (size_t i = 1; i < effectiveCount; ++i) {
        if (samples[i] < minVal) minVal = samples[i];
        if (samples[i] > maxVal) maxVal = samples[i];
    }
    threshold_ = (static_cast<int>(minVal) + static_cast<int>(maxVal)) / 2;
    threshold_computed_ = true;
    LOG_DEBUG("[BitstreamRecoverer] Threshold computed: %d (min=%d, max=%d)",
              threshold_, (int)minVal, (int)maxVal);
}

void BitstreamRecoverer::setThreshold(int threshold) {
    threshold_ = threshold;
    threshold_computed_ = true;
}

void BitstreamRecoverer::outputBit(uint8_t bit) {
    fifo_.push(bit);
}

void BitstreamRecoverer::processSample(int16_t sample) {


    // Если порог ещё не вычислен, накапливаем отсчёты
    if (!threshold_computed_) {
        // Если порог ещё не установлен, игнорируем отсчёты
        return;
    }

    bool above = (sample >= threshold_);
    uint8_t outBit;
    if (polarity_ == NORMAL) {
        outBit = above ? 0 : 1;
    } else {
        outBit = above ? 1 : 0;
    }

    if (above) {
        ++numUp_;
        int step = calcOutStep(numDown_);
        if (numUp_ == 1 && step == 0) step = 1;
        for (int i = 0; i < step; ++i) outputBit(outBit);
        numDown_ = 0;
    } else {
        ++numDown_;
        int step = calcOutStep(numUp_);
        if (numDown_ == 1 && step == 0) step = 1;
        for (int i = 0; i < step; ++i) outputBit(outBit);
        numUp_ = 0;
    }


}

void BitstreamRecoverer::processSamples(const int16_t* samples, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        processSample(samples[i]);
    }
}

void BitstreamRecoverer::reset() {
    numUp_ = 0;
    numDown_ = 0;
    // порог не сбрасываем
}

void BitstreamRecoverer::flush() {
    if (!threshold_computed_) return;
    if (numUp_ > 0) {
        int step = calcOutStep(numUp_);
        uint8_t outBit = (polarity_ == NORMAL) ? 0 : 1;
        for (int i = 0; i < step; ++i) outputBit(outBit);
        numUp_ = 0;
    }
    if (numDown_ > 0) {
        int step = calcOutStep(numDown_);
        uint8_t outBit = (polarity_ == NORMAL) ? 1 : 0;
        for (int i = 0; i < step; ++i) outputBit(outBit);
        numDown_ = 0;
    }
}

} // namespace orbita