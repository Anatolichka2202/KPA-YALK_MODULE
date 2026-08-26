#pragma once

#include <memory>
#include <string>
#include <vector>

namespace orbita::stand {

struct VisaInstrumentConfig {
    std::vector<std::string> resourceExpressions;
    unsigned timeoutMilliseconds = 2000;
};

class VisaInstrument final {
public:
    explicit VisaInstrument(VisaInstrumentConfig config);
    ~VisaInstrument();
    VisaInstrument(const VisaInstrument&) = delete;
    VisaInstrument& operator=(const VisaInstrument&) = delete;
    VisaInstrument(VisaInstrument&&) noexcept;
    VisaInstrument& operator=(VisaInstrument&&) noexcept;

    void write(const std::string& command);
    std::string query(const std::string& command, unsigned delayMilliseconds = 45);
    const std::string& resourceName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orbita::stand
