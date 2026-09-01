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

} // namespace orbita::stand
