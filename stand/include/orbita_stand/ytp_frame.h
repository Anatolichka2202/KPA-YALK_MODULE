#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace orbita::stand {

// Формат доказан только для архивной прошивки адаптера в WorkMode=2:
// 32 little-endian слова (30 каналов + minimum + maximum) и байт TempMode.
struct YtpLegacyMode2Frame {
    std::array<std::uint16_t, 30> channelRaw{};
    std::uint16_t calibrationMinimumRaw = 0;
    std::uint16_t calibrationMaximumRaw = 0;
    std::uint8_t temperatureMode = 0;
};

YtpLegacyMode2Frame decodeYtpLegacyMode2Frame(
    const std::vector<std::uint8_t>& payload);

// Фактический кадр текущего стенда после ROKT 0A 02 00 01 00:
// 4 байта заголовка и 32 little-endian слова. Семантика заголовка и
// назначение последних двух слов пока не выводятся из архивной прошивки.
struct YtpRokt68Frame {
    std::array<std::uint8_t, 4> header{};
    std::array<std::uint16_t, 30> channelRaw{};
    std::uint16_t calibrationCandidate31Raw = 0;
    std::uint16_t calibrationCandidate32Raw = 0;
};

YtpRokt68Frame decodeYtpRokt68Frame(
    const std::vector<std::uint8_t>& payload);

bool isYtpNoMeasurementRaw(std::uint16_t raw) noexcept;

} // namespace orbita::stand
