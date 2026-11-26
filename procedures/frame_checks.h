#pragma once

#include "orbita.h"

#include <cstdint>
#include <array>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace miltech::brp {

constexpr unsigned counterLimit = 1023;
constexpr int counterBaud = 500000;
constexpr std::size_t groupWords = 2048;
constexpr std::array<unsigned, 4> simulatedYasmPattern{0x12, 0x1B2, 0x15C, 0x67};

// Decode the returning counter for both real and simulated acquisition.
// Never gate this check on whether the station is running in simulation.
constexpr bool shouldObserveCounter(bool yasmMode, bool monitorMode, bool monitorCounter)
{
    return (!yasmMode && !monitorMode) || monitorCounter;
}

// Два байта со взаимно исключающими признаками младшей и старшей частей.
// Это шаблон стендового воздействия для прозрачного RS-485, а не протокол БРП.
std::vector<uint8_t> counterBytes(unsigned& next, unsigned count);
uint16_t simulatedWord(std::size_t word);

class CounterCheck {
public:
    void consume(const std::vector<uint16_t>& words, int group, int cycle);
    bool passed() const { return contiguous_ >= 2 * (counterLimit + 1) + 1; }
    std::vector<unsigned> trace() const { return {trace_.begin(), trace_.end()}; }
    // Recent decoded values survive continuity gaps. For live diagnostics only.
    std::vector<unsigned> recent() const { return {recent_.begin(), recent_.end()}; }
    unsigned contiguous() const { return contiguous_; }
    unsigned groupsReceived() const { return groupsReceived_; }
    unsigned markedWords() const { return markedWords_; }
    unsigned decodedValues() const { return decodedValues_; }
    unsigned breaks() const { return breaks_; }
    std::optional<unsigned> latestValue() const { return latestValue_; }

private:
    void interrupt();
    int group_ = 0;
    int cycle_ = 0;
    std::optional<unsigned> low_;
    std::optional<unsigned> previous_;
    unsigned contiguous_ = 0;
    std::deque<unsigned> trace_;
    std::deque<unsigned> recent_;
    unsigned groupsReceived_ = 0;
    unsigned markedWords_ = 0;
    unsigned decodedValues_ = 0;
    unsigned breaks_ = 0;
    std::optional<unsigned> latestValue_;
};

// Входной тест ЯСМ выполняется на внешнем источнике. Здесь сверяются
// известные коды, уже принятые по адресу МКО в кадре M16.
class YasmCheck {
public:
    explicit YasmCheck(std::vector<unsigned> expected);
    static YasmCheck fromBusWords(const std::vector<uint16_t>& words);
    void consume(const std::vector<uint16_t>& words, int group, int cycle);
    bool passed() const { return passed_; }
    std::vector<unsigned> trace() const { return {recent_.begin(), recent_.end()}; }

private:
    std::vector<unsigned> expected_;
    std::vector<unsigned> alternate_;
    std::deque<unsigned> recent_;
    int lastOrdinal_ = -1;
    bool passed_ = false;
};

struct VoltageScale {
    double minVolts;
    double maxVolts;
    unsigned minCode;
    unsigned maxCode;
    std::optional<double> tolerancePercent;

    double volts(unsigned code) const;
};

struct FrameCheck {
    int number;
    std::string title;
    std::string requirement;
    std::string cell;
    std::string note;
    std::vector<std::string> addresses;
    std::vector<std::string> channelNames;
    std::vector<orbita::M16GroupChannel> layouts;
    std::vector<unsigned> shifts;
    unsigned bits;
    std::optional<VoltageScale> scale;
    std::string voltagePointRequirement;
    std::vector<double> pointTargets; // Optional multi-point sweep from the profile.
    double targetWindowVolts = 0.5; // Setup validation, not an acceptance tolerance.
};

std::vector<FrameCheck> loadFrameChecks(const std::string& yaml);

struct CodeRange {
    unsigned count = 0;
    unsigned minimum = 0;
    unsigned maximum = 0;
    unsigned fillCount = 0;
    void consume(const std::vector<uint16_t>& words, const FrameCheck& check, std::size_t channel);
    void merge(const CodeRange& group);
    bool matches(unsigned expectedMin, unsigned expectedMax) const;
};

struct VoltageComparison {
    double minimumVolts;
    double maximumVolts;
    double maximumErrorVolts;
    std::optional<double> toleranceVolts;
};

VoltageComparison compareVoltage(const VoltageScale& scale, const CodeRange& codes,
                                 double before, double after);

} // namespace miltech::brp
