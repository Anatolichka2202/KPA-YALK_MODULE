
#include "bitstream_recoverer.h"
#include <cmath>
#include <algorithm>

namespace orbita {

static constexpr double FREQ_RATIO = 10.0 / 3.145728; // ~3.179 (отношение частот)

BitstreamRecoverer::BitstreamRecoverer(FifoBuffer& output_fifo, Polarity polarity)
    : fifo_(output_fifo)
    , polarity_(polarity)
    , threshold_(0)
    , threshold_computed_(false)
    , numUp_(0)
    , numDown_(0)
{
}

void BitstreamRecoverer::computeThreshold(const int16_t* samples, size_t count) {
    if (count == 0) return;
    int16_t minVal = samples[0];
    int16_t maxVal = samples[0];
    for (size_t i = 1; i < count; ++i) {
        if (samples[i] < minVal) minVal = samples[i];
        if (samples[i] > maxVal) maxVal = samples[i];
    }
    threshold_ = (static_cast<int>(minVal) + static_cast<int>(maxVal)) / 2;
    threshold_computed_ = true;
}

void BitstreamRecoverer::setThreshold(int threshold) {
    threshold_ = threshold;
    threshold_computed_ = true;
}

int BitstreamRecoverer::calcOutStep(int count) {
    if (count == 0) return 0;
    return static_cast<int>(std::round(count / FREQ_RATIO));
}

void BitstreamRecoverer::outputBit(uint8_t bit) {
    fifo_.push(bit);
}

void BitstreamRecoverer::processSample(int16_t sample) {
    if (!threshold_computed_) return;

    bool above = (sample >= threshold_);
    uint8_t rawBit = above ? 1 : 0;
    // Применяем полярность: NORMAL: high->0, low->1; INVERTED: high->1, low->0
    uint8_t outBit = (polarity_ == NORMAL) ? (1 - rawBit) : rawBit;

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

} // namespace orbita
