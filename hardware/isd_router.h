#pragma once

#include "hardware/stand_config.h"

#include <functional>
#include <memory>
#include <string>

namespace tu::hardware {

class IsdRouter final
{
public:
    using TraceSink = std::function<void(const std::string& path,
                                         int httpStatus,
                                         const std::string& response)>;

    explicit IsdRouter(IsdConfig config);
    ~IsdRouter();

    IsdRouter(const IsdRouter&) = delete;
    IsdRouter& operator=(const IsdRouter&) = delete;

    std::string probe();
    void setSwitch(unsigned type, unsigned channel, bool enabled);
    void setAnalog(unsigned channel, unsigned value, bool enabled);
    void setYalkVoltage(unsigned channel, double volts);
    void disableYalkOutput(unsigned channel);
    // Observability only.  Commands remain one-shot: the sink cannot cause a
    // retry or alter the acknowledgement decision.
    void setTraceSink(TraceSink sink);
    void safeStop() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tu::hardware
