#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace orbita::stand {

enum class Verdict {
    Ok,
    Fail,
    Error,
};

struct YalkCalibration {
    double zeroCode = 0.0;
    double fullScaleCode = 0.0;
};

struct YalkTestPoint {
    double commandVolts = 0.0;
    std::optional<bool> expectedSignal;
};

struct YalkAnalogProcedureConfig {
    double fullScaleVolts = 6.2;
    double allowedErrorPercentFullScale = 0.5;
    std::size_t sampleCount = 16;
    unsigned stabilizationMilliseconds = 150;
    std::vector<YalkTestPoint> points{
        {0.0, false},
        {3.1, std::nullopt},
        {6.2, true},
    };
};

struct YalkPointResult {
    double commandVolts = 0.0;
    double referenceVolts = 0.0;
    double averageRawCode = 0.0;
    double measuredYalkVolts = 0.0;
    double absoluteErrorVolts = 0.0;
    double errorPercentFullScale = 0.0;
    double lowerLimitVolts = 0.0;
    double upperLimitVolts = 0.0;
    std::optional<bool> expectedSignal;
    std::optional<bool> actualSignal;
    Verdict verdict = Verdict::Error;
    std::string message;
};

struct YalkProcedureResult {
    unsigned channel = 0;
    YalkCalibration calibration;
    std::vector<YalkPointResult> points;
    Verdict verdict = Verdict::Error;
    std::string message;
};

class IIsdRouter {
public:
    virtual ~IIsdRouter() = default;
    virtual void reset() = 0;
    virtual void connectChannel(unsigned channel) = 0;
    virtual void disconnectChannel(unsigned channel) = 0;
};

class IVoltageSource {
public:
    virtual ~IVoltageSource() = default;
    virtual void setVoltage(double volts) = 0;
    virtual void outputOn() = 0;
    virtual void outputOff() = 0;
};

class IReferenceVoltmeter {
public:
    virtual ~IReferenceVoltmeter() = default;
    virtual double readVoltage() = 0;
};

class IYalkReader {
public:
    virtual ~IYalkReader() = default;
    virtual YalkCalibration readCalibration(unsigned channel) = 0;
    virtual std::vector<double> readRawCodes(unsigned channel, std::size_t count) = 0;
    virtual bool readSignal(unsigned channel) = 0;
};

class IProcedureWaiter {
public:
    virtual ~IProcedureWaiter() = default;
    virtual void waitMilliseconds(unsigned milliseconds) = 0;
};

class CheckYalkAnalogChannel {
public:
    explicit CheckYalkAnalogChannel(YalkAnalogProcedureConfig config = {});

    YalkProcedureResult execute(
        unsigned channel,
        IIsdRouter& isd,
        IVoltageSource& source,
        IReferenceVoltmeter& voltmeter,
        IYalkReader& yalk,
        IProcedureWaiter& waiter) const;

    static double codeToVolts(
        double rawCode,
        const YalkCalibration& calibration,
        double fullScaleVolts);

private:
    YalkAnalogProcedureConfig config_;
};

// Совместимость исходного API. Новое имя подчёркивает, что это атомарная
// процедура сценарного движка, а не самостоятельный сценарий испытаний.
using YalkAnalogProcedure = CheckYalkAnalogChannel;

} // namespace orbita::stand
