#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tu::hardware {

struct VisaConfig {
    std::vector<std::string> resources;
    unsigned timeoutMilliseconds = 2000;
};

class VisaInstrument final
{
public:
    explicit VisaInstrument(VisaConfig config);
    ~VisaInstrument();
    VisaInstrument(const VisaInstrument&) = delete;
    VisaInstrument& operator=(const VisaInstrument&) = delete;

    void write(const std::string& command);
    std::string query(const std::string& command, unsigned delayMilliseconds = 45);
    void reconnect();
    const std::string& resourceName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tu::hardware
