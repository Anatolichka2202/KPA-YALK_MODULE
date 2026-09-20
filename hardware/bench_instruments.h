#pragma once

#include "hardware/stand_config.h"
#include "hardware/visa_instrument.h"

#include <memory>
#include <string>

namespace tu::hardware {

class V7Meter final
{
public:
    explicit V7Meter(V7Config config);
    ~V7Meter();

    std::string identity();
    double readDcVoltage();
    double readAcVoltage();
    double readFrequency();
    const std::string& resourceName() const;

private:
    V7Config config_;
    VisaInstrument instrument_;
};

class RigolGenerator final
{
public:
    explicit RigolGenerator(RigolConfig config);
    ~RigolGenerator();

    std::string identity();
    void setSine(unsigned channel, double frequencyHz, double amplitudeVpp,
                 double offsetVolts = 0.0);
    void output(unsigned channel, bool enabled);
    void safeOff() noexcept;
    const std::string& resourceName() const;

private:
    RigolConfig config_;
    VisaInstrument instrument_;
};

} // namespace tu::hardware
