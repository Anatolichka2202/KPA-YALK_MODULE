#pragma once

#include "frame_decoder.h"
#include "../tlm/tlm_writer.h"

#include <chrono>
#include <memory>
#include <optional>
#include <utility>

namespace orbita {

class FrameDecoderM8 final : public FrameDecoder {
public:
    explicit FrameDecoderM8(double samplesPerSecond);

    void appendSamples(const std::vector<int16_t>& samples, bool inverted);
    void process() override;
    void reset() override;
    void setGroupCallback(GroupCallback callback) override { callback_ = std::move(callback); }
    void getErrors(int& phraseError, int& groupError) const override;
    uint64_t getTotalGroups() const override { return decodedGroups_; }
    void startTlm(const std::string& filename) override;
    void stopTlm() override;
    bool isTlmActive() const override { return static_cast<bool>(writer_); }

private:
    static constexpr unsigned wordsPerPhrase = 8;
    static constexpr unsigned wordsPerGroup = wordsPerPhrase * 128;
    static constexpr unsigned bitsPerWord = 12;
    static constexpr unsigned bitsPerPair = 2 * wordsPerPhrase * bitsPerWord;
    static constexpr uint8_t groupMarker = 0x72;
    static constexpr uint8_t cycleMarker = 0x8D;

    bool high(std::size_t index) const;
    bool isFallingMarker(std::size_t edge) const;
    void decodePhrasePair(std::size_t begin, std::size_t end);
    void loseGroupSync();
    void publishGroup();

    const double samplesPerBit_;
    const double samplesPerPair_;
    const std::size_t halfWidth_;
    std::vector<int16_t> samples_;
    std::optional<int> threshold_;
    std::size_t scan_ = 0;
    uint64_t discardedSamples_ = 0;
    std::optional<uint64_t> lastRise_;
    std::optional<std::size_t> marker_;
    bool inverted_ = false;

    std::vector<uint16_t> groupWords_;
    uint8_t markerBits_ = 0;
    int group_ = 0;
    int cycle_ = 0;
    uint64_t phraseChecks_ = 0, phraseErrors_ = 0;
    uint64_t groupChecks_ = 0, groupErrors_ = 0, decodedGroups_ = 0;
    GroupCallback callback_;

    std::unique_ptr<TlmWriter> writer_;
    std::chrono::steady_clock::time_point recordStart_;
    uint32_t recordBlock_ = 0;
    bool recordCycleOpen_ = false;
};

} // namespace orbita
