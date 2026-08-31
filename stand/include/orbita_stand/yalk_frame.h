#ifndef YALK_FRAME_H
#define YALK_FRAME_H
#pragma once
#include <cstdint>
#include <vector>

namespace orbita::stand {
struct YalkSample {
    std::uint16_t rawWord = 0;
    std::uint16_t analogCode = 0;
    bool contact = false;
};
YalkSample decodeYalkSample (std::uint16_t rawWord);
std::vector<YalkSample> decodeYalkSlowFrame(const std::vector<std::uint8_t>& frame);
std::vector<YalkSample> decodeYalkReferenceFrame(
    const std::vector<std::uint8_t>& frame);
}

#endif // YALK_FRAME_H
