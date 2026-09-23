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
    using LiveYalkSink = std::function<void(
        const std::vector<YalkChannelReading>&, std::uint64_t)>;
    using LiveYtpSink = std::function<void(const YtpSnapshot&, std::uint64_t)>;

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
    // Marks the beginning of an operator-controlled YTP point.  The receiver
    // continues to drain UDP while the dialog is open; on confirmation the
    // procedure consumes the most recent frames received after this marker.
    std::uint64_t markYtpFrames() const;
    YtpSnapshot readYtpSnapshotSince(std::uint64_t marker,
                                     unsigned sampleCount,
                                     std::chrono::milliseconds timeout,
                                     const Checkpoint& checkpoint);

    // The UDP receiver has a single owner thread. The live sink receives a
    // throttled copy of fresh reference204 frames while procedure sampling
    // independently waits for frames newer than its own freshness barrier.
    void setLiveYalkSink(LiveYalkSink sink);
    void setLiveYtpSink(LiveYtpSink sink);

    void stop() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tu::hardware
