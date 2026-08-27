#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace orbita::stand {

struct DhoPreamble {
    int format = 0;
    int type = 0;
    long points = 0;
    int count = 0;
    double xIncrement = 0.0;
    double xOrigin = 0.0;
    double xReference = 0.0;
    double yIncrement = 0.0;
    double yOrigin = 0.0;
    double yReference = 0.0;
    bool valid = false;
};

struct DhoWaveform {
    DhoPreamble preamble;
    std::vector<double> seconds;
    std::vector<double> volts;
};

DhoPreamble parseDhoPreamble(const std::string& response);
DhoWaveform decodeDhoWordWaveform(
    const std::string& preamble,
    const std::vector<std::uint8_t>& tmcBlock);

} // namespace orbita::stand
