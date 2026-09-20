#pragma once

#include "hardware/stand_config.h"

#include <memory>
#include <string>

namespace tu::hardware {

struct PowerState {
    double measuredVoltageV = 0.0;
    double measuredCurrentA = 0.0;
    double voltageSetpointV = 0.0;
    double overvoltageLimitV = 0.0;
    double currentSetpointA = 0.0;
    bool outputEnabled = false;
};

class Akip1160 final {
public:
    explicit Akip1160(AkipConfig config);
    ~Akip1160();

    Akip1160(const Akip1160&) = delete;
    Akip1160& operator=(const Akip1160&) = delete;

    std::string probe() const;
    PowerState readState() const;

    void setVoltage(double volts);
    void setCurrentLimit(double amperes);
    void setOutput(bool enabled);
    void safeOff() noexcept;

    const AkipConfig& config() const noexcept { return config_; }

private:
    class Serial;
    void verifyNear(double actual, double expected, double tolerance,
                    const std::string& what) const;
    void confirmOutputOff();

    AkipConfig config_;
    std::unique_ptr<Serial> serial_;
    bool voltageArmed_ = false;
    bool currentArmed_ = false;
};

} // namespace tu::hardware
