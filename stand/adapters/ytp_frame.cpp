#include "orbita_stand/ytp_frame.h"

#include <stdexcept>

namespace orbita::stand {

YtpLegacyMode2Frame decodeYtpLegacyMode2Frame(
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() != 65) {
        throw std::invalid_argument(
            "Архивный кадр ЯТП mode 2 должен содержать ровно 65 байт");
    }

    const auto word = [&payload](std::size_t index) {
        const std::size_t offset = index * 2;
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(payload[offset])
            | (static_cast<std::uint16_t>(payload[offset + 1]) << 8));
    };

    YtpLegacyMode2Frame result;
    for (std::size_t index = 0; index < result.channelRaw.size(); ++index) {
        result.channelRaw[index] = word(index);
    }
    result.calibrationMinimumRaw = word(30);
    result.calibrationMaximumRaw = word(31);
    result.temperatureMode = payload[64];
    return result;
}

} // namespace orbita::stand
