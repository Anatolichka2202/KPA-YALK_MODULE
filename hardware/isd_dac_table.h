#pragma once

#include <array>
#include <cmath>
#include <stdexcept>

namespace tu::hardware {

// Экспериментальная таблица код/напряжение, снятая  по В7.
// Промежуточные точки интерполируются; фактическое напряжение всегда читает В7.
inline unsigned isdDacCode(double volts)
{
    constexpr std::array<unsigned, 21> codes{
        0, 205, 410, 614, 819, 1024, 1229, 1433, 1638, 1843, 2048,
        2252, 2457, 2662, 2867, 3071, 3276, 3481, 3686, 3890, 4095};
    if (!std::isfinite(volts) || volts < -2.0 || volts > 8.0)
        throw std::invalid_argument("Напряжение ИСД вне таблицы -2...8 В");
    const double position = (volts + 2.0) * 2.0;
    const auto lower = static_cast<unsigned>(std::floor(position));
    if (lower >= codes.size() - 1) return codes.back();
    const double fraction = position - static_cast<double>(lower);
    return static_cast<unsigned>(std::lround(
        codes[lower] + fraction * (codes[lower + 1] - codes[lower])));
}

} // namespace tu::hardware
