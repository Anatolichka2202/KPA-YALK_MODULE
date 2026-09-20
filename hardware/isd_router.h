#pragma once

#include "hardware/stand_config.h"

#include <memory>
#include <string>

namespace tu::hardware {

class IsdRouter final
{
public:
    explicit IsdRouter(IsdConfig config);
    ~IsdRouter();

    IsdRouter(const IsdRouter&) = delete;
    IsdRouter& operator=(const IsdRouter&) = delete;

    std::string probe();
    void serviceFullReset();
    void setSwitch(unsigned type, unsigned channel, bool enabled);
    void setAnalog(unsigned channel, unsigned value, bool enabled);
    void setYalkVoltage(unsigned channel, double volts);
    void disableYalkOutput(unsigned channel);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tu::hardware
