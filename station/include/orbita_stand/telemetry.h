#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace orbita::stand {

enum class SampleQuality : std::uint8_t {
    Good,
    Stale,
    Invalid,
    Disconnected,
};

struct ParameterSample {
    std::string parameterKey;
    std::chrono::system_clock::time_point timestamp{};
    std::uint64_t sequence = 0;
    double rawValue = 0.0;
    double physicalValue = 0.0;
    std::string unit;
    SampleQuality quality = SampleQuality::Invalid;
    std::string diagnostic;
};

class IParameterSource {
public:
    virtual ~IParameterSource() = default;
    virtual std::string sourceId() const = 0;
    virtual std::string probe() = 0;
    virtual ParameterSample read(const std::string& parameterKey) = 0;
    virtual void safeStop() noexcept = 0;
};

} // namespace orbita::stand
