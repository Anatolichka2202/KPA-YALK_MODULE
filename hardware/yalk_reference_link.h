#pragma once

#include "hardware/stand_config.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace tu::hardware {

struct YalkChannelReading {
    double rawMean = 0.0;
    double codeMean = 0.0;
    bool contact = false;
};

struct YtpSnapshot {
    std::array<double, 30> channels{};
    double calibration31 = 0.0;
    double calibration32 = 0.0;
    unsigned validWordCount = 0;
};

class YalkReferenceLink final {
public:
    using Checkpoint = std::function<void()>;

    explicit YalkReferenceLink(YalkUdpConfig config);
    ~YalkReferenceLink();

    YalkReferenceLink(const YalkReferenceLink&) = delete;
    YalkReferenceLink& operator=(const YalkReferenceLink&) = delete;

    bool restartUntilReady(std::chrono::milliseconds timeout,
                           const Checkpoint& checkpoint);
    bool waitNextReference(std::chrono::milliseconds timeout,
                           const Checkpoint& checkpoint);

    bool startYalk(std::chrono::milliseconds configureSettle,
                   std::chrono::milliseconds timeout,
                   const Checkpoint& checkpoint);
    std::vector<YalkChannelReading> readYalkSnapshot(
        unsigned sampleCount, std::chrono::milliseconds timeout,
        const Checkpoint& checkpoint);

    bool startYtp(unsigned endpoint,
                  std::chrono::milliseconds configureSettle,
                  std::chrono::milliseconds streamSettle,
                  std::chrono::milliseconds timeout,
                  const Checkpoint& checkpoint);
    YtpSnapshot readYtpSnapshot(unsigned sampleCount,
                                std::chrono::milliseconds timeout,
                                const Checkpoint& checkpoint);

    void stop() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tu::hardware
