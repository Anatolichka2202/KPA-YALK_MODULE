#include "orbita_stand/dho_waveform.h"

#include <charconv>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace orbita::stand {
namespace {

std::vector<std::string_view> split(std::string_view value)
{
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (true) {
        const auto comma = value.find(',', start);
        if (comma == std::string_view::npos) { result.push_back(value.substr(start)); break; }
        result.push_back(value.substr(start, comma - start));
        start = comma + 1;
    }
    return result;
}

template<typename T> bool number(std::string_view value, T& output)
{
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) value.remove_suffix(1);
    while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output);
    return parsed.ec == std::errc() && parsed.ptr == value.data() + value.size();
}

std::pair<const std::uint8_t*, std::size_t> tmcPayload(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() < 3 || bytes[0] != '#' || bytes[1] < '1' || bytes[1] > '9') {
        throw std::runtime_error("DHO8xx returned an invalid IEEE 488.2 block");
    }
    const std::size_t digits = bytes[1] - '0';
    if (bytes.size() < 2 + digits) throw std::runtime_error("DHO8xx returned a truncated block header");
    std::size_t length = 0;
    for (std::size_t i = 0; i < digits; ++i) {
        const auto character = bytes[2 + i];
        if (character < '0' || character > '9') throw std::runtime_error("DHO8xx block length is invalid");
        length = length * 10 + character - '0';
    }
    const std::size_t offset = 2 + digits;
    if (bytes.size() - offset < length) throw std::runtime_error("DHO8xx returned a truncated waveform");
    return {bytes.data() + offset, length};
}

} // namespace

DhoPreamble parseDhoPreamble(const std::string& response)
{
    DhoPreamble result;
    const auto values = split(response);
    long format = 0, type = 0, points = 0, count = 0;
    if (values.size() != 10
        || !number(values[0], format) || !number(values[1], type)
        || !number(values[2], points) || !number(values[3], count)
        || !number(values[4], result.xIncrement) || !number(values[5], result.xOrigin)
        || !number(values[6], result.xReference) || !number(values[7], result.yIncrement)
        || !number(values[8], result.yOrigin) || !number(values[9], result.yReference)) return result;
    result.format = static_cast<int>(format);
    result.type = static_cast<int>(type);
    result.points = points;
    result.count = static_cast<int>(count);
    result.valid = points > 0 && result.xIncrement > 0.0 && result.yIncrement > 0.0;
    return result;
}

DhoWaveform decodeDhoWordWaveform(
    const std::string& preamble,
    const std::vector<std::uint8_t>& tmcBlock)
{
    DhoWaveform waveform;
    waveform.preamble = parseDhoPreamble(preamble);
    if (!waveform.preamble.valid || waveform.preamble.format != 1) {
        throw std::runtime_error("DHO8xx WORD preamble is invalid or unsupported");
    }
    const auto [data, size] = tmcPayload(tmcBlock);
    if (size % 2) throw std::runtime_error("DHO8xx WORD payload has an odd byte count");
    const std::size_t points = size / 2;
    waveform.seconds.reserve(points);
    waveform.volts.reserve(points);
    for (std::size_t index = 0; index < points; ++index) {
        const std::uint16_t raw = static_cast<std::uint16_t>(data[index * 2])
            | static_cast<std::uint16_t>(data[index * 2 + 1]) << 8;
        waveform.seconds.push_back((static_cast<double>(index) - waveform.preamble.xReference)
            * waveform.preamble.xIncrement + waveform.preamble.xOrigin);
        waveform.volts.push_back((static_cast<double>(raw) - waveform.preamble.yOrigin
            - waveform.preamble.yReference) * waveform.preamble.yIncrement);
    }
    return waveform;
}

} // namespace orbita::stand
