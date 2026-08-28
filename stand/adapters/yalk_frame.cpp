#include "orbita_stand/yalk_frame.h"
#include <stdexcept>

namespace orbita::stand {
YalkSample decodeYalkSample(std::uint16_t rawWord){
    YalkSample Sample;
    Sample.analogCode = rawWord & 0x01FF;
    Sample.contact = (rawWord & 0x0200) != 0;
    Sample.rawWord = rawWord;

    return Sample;
}
std::vector<YalkSample> decodeYalkSlowFrame(
    const std::vector<std::uint8_t>& frame)
{
    if (frame.size() != 200) {
        throw std::invalid_argument(
            "YALK slow frame must contain 200 bytes");
    }

    std::vector<YalkSample> result;
    result.reserve(100);

    for (std::size_t w = 0; w < 100; ++w) {
        const std::uint16_t low =
            static_cast<std::uint16_t>(frame[2 * w]);

        const std::uint16_t high =
            static_cast<std::uint16_t>(frame[2 * w + 1]);

        const std::uint16_t word =
            low | (high << 8);

        result.push_back(decodeYalkSample(word));
    }

    return result;
}

}
