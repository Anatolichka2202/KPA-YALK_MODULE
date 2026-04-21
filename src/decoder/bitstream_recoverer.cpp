#include "bitstream_recoverer.h"
#include <cmath>
#include <algorithm>
#include <QDebug>

namespace orbita {

// Константа отношения частот: (10 МГц АЦП) / (3.145728 МГц частота битов) ≈ 3.179
static constexpr long double FREQ_RATIO = 10.0L / 3.145728L;

// Банковское округление (как в Delphi)
int BitstreamRecoverer::bankersRound(long double value) {
    long double frac = std::fmod(value, 1.0L);
    long double intPart = std::floor(value);
    if (frac < 0.5L)
        return static_cast<int>(intPart);
    if (frac > 0.5L)
        return static_cast<int>(intPart + 1.0L);
    // frac == 0.5
    int intVal = static_cast<int>(intPart);
    return (intVal % 2 == 0) ? intVal : intVal + 1;
}

int BitstreamRecoverer::calcOutStep(int count) {
    if (count == 0) return 0;
    const long double RATIO = 10.0L / 3.145728L;
    long double value = count / RATIO;
    long double intPart;
    long double frac = modfl(value, &intPart);
    int intVal = static_cast<int>(intPart);
    if (frac < 0.5L) return intVal;
    if (frac > 0.5L) return intVal + 1;
    return (intVal % 2 == 0) ? intVal : intVal + 1;
}

BitstreamRecoverer::BitstreamRecoverer(FifoBuffer& output_fifo, Polarity polarity)
    : fifo_(output_fifo)
    , polarity_(polarity)
    , threshold_(0)
    , threshold_computed_(false)
    , numUp_(0)
    , numDown_(0)
{
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
    qDebug() << "[BitstreamRecoverer] Threshold computed:" << threshold_
             << "(min=" << minVal << ", max=" << maxVal
             << ", samples used=" << effectiveCount << ")";
}

void BitstreamRecoverer::setThreshold(int threshold) {
    threshold_ = threshold;
    threshold_computed_ = true;
}

void BitstreamRecoverer::outputBit(uint8_t bit) {
    fifo_.push(bit);
}

void BitstreamRecoverer::processSample(int16_t sample) {
    if (!threshold_computed_) return;

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
    static int sampleCount = 0;
    if (sampleCount++ < 200) {
        qDebug() << "sample=" << sample << " above=" << above << " outBit=" << outBit;
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
    // Порог не сбрасываем
}

void BitstreamRecoverer::flush() {
    if (!threshold_computed_) return;
    // Если осталась серия выше порога
    if (numUp_ > 0) {
        int step = calcOutStep(numUp_);
        uint8_t outBit = (polarity_ == NORMAL) ? 0 : 1;
        for (int i = 0; i < step; ++i) outputBit(outBit);
        numUp_ = 0;
    }
    // Если осталась серия ниже порога
    if (numDown_ > 0) {
        int step = calcOutStep(numDown_);
        uint8_t outBit = (polarity_ == NORMAL) ? 1 : 0;
        for (int i = 0; i < step; ++i) outputBit(outBit);
        numDown_ = 0;
    }
}

} // namespace orbita
