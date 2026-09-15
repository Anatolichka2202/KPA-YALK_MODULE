#pragma once

#include "orbita_stand/equipment_contracts.h"

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

// YALK-specific reader contract stays with the YALK procedure because its
// calibration and raw-code semantics are part of this delivery/domain model.
class IYalkReader {
public:
    virtual ~IYalkReader() = default;
    virtual YalkCalibration readCalibration(unsigned channel) = 0;
    virtual std::vector<double> readRawCodes(unsigned channel, std::size_t count) = 0;
    virtual bool readSignal(unsigned channel) = 0;
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

using YalkAnalogProcedure = CheckYalkAnalogChannel;

} // namespace orbita::stand
