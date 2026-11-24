#pragma once

#include "orbita.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace miltech::brp {

constexpr unsigned counterLimit = 1023;
constexpr int counterBaud = 500000;
constexpr std::size_t groupWords = 2048;

// Два байта со взаимно исключающими признаками младшей и старшей частей.
// Это шаблон стендового воздействия для прозрачного RS-485, а не протокол БРП.
std::vector<uint8_t> counterBytes(unsigned& next, unsigned count);
uint16_t simulatedWord(std::size_t word);

class CounterCheck {
public:
    void consume(const std::vector<uint16_t>& words, int group, int cycle);
    bool passed() const { return contiguous_ >= 2 * (counterLimit + 1) + 1; }
    std::vector<unsigned> trace() const { return {trace_.begin(), trace_.end()}; }
    unsigned contiguous() const { return contiguous_; }

private:
    void interrupt();
    int group_ = 0;
    int cycle_ = 0;
    std::optional<unsigned> low_;
    std::optional<unsigned> previous_;
    unsigned contiguous_ = 0;
    std::deque<unsigned> trace_;
};

struct FrameCheck {
    int number;
    std::string title;
    std::string requirement;
    std::string cell;
    std::string note;
    std::vector<std::string> addresses;
    std::vector<orbita::M16GroupChannel> layouts;
    unsigned shift;
    unsigned bits;
};

std::vector<FrameCheck> loadFrameChecks(const std::string& yaml);

struct CodeRange {
    unsigned count = 0;
    unsigned minimum = 0;
    unsigned maximum = 0;
    void consume(const std::vector<uint16_t>& words, const FrameCheck& check, std::size_t channel);
    bool matches(unsigned expectedMin, unsigned expectedMax) const;
};

} // namespace miltech::brp
