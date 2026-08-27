#pragma once

#include "orbita_stand/yalk_analog_procedure.h"

#include <memory>
#include <string>

namespace orbita::stand {

struct V7VisaConfig {
    std::string resourceExpression = "USB[0-9]*::0x164E::0x0DAD::?*INSTR";
    // Старый NI-VISA и референсная Delphi-программа используют десятичную
    // запись VID/PID как запасной вариант для того же прибора.
    std::string fallbackResourceExpression = "USB[0-9]*::5710::3501::?*INSTR";
    unsigned timeoutMilliseconds = 2000;
    unsigned readDelayMilliseconds = 45;
    std::string voltageCommand = "READ?";
    std::string currentCommand = "MEAS:CURR:DC?";
    std::string acVoltageCommand = "MEAS:VOLT:AC?";
    std::string frequencyCommand = "MEAS:FREQ?";
};

// Windows adapter for the V7-78/1. NI-VISA is loaded at runtime, so the
// portable domain library does not require vendor headers or import libraries.
class V7VisaVoltmeter final : public IReferenceVoltmeter {
public:
    explicit V7VisaVoltmeter(V7VisaConfig config = {});
    ~V7VisaVoltmeter() override;

    V7VisaVoltmeter(const V7VisaVoltmeter&) = delete;
    V7VisaVoltmeter& operator=(const V7VisaVoltmeter&) = delete;
    V7VisaVoltmeter(V7VisaVoltmeter&&) noexcept;
    V7VisaVoltmeter& operator=(V7VisaVoltmeter&&) noexcept;

    double readVoltage() override;
    double readCurrent();
    double readAcVoltage();
    double readFrequency();
    const std::string& resourceName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orbita::stand
