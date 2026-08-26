#include "orbita_stand/yalk_analog_procedure.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace orbita::stand {
namespace {

bool isFinite(double value)
{
    return std::isfinite(value);
}

void validateConfig(const YalkAnalogProcedureConfig& config)
{
    if (!isFinite(config.fullScaleVolts) || config.fullScaleVolts <= 0.0) {
        throw std::invalid_argument("YALK full-scale voltage must be positive");
    }
    if (!isFinite(config.allowedErrorPercentFullScale)
        || config.allowedErrorPercentFullScale < 0.0) {
        throw std::invalid_argument("YALK allowed error must be non-negative");
    }
    if (config.sampleCount == 0) {
        throw std::invalid_argument("YALK sample count must be positive");
    }
    if (config.points.empty()) {
        throw std::invalid_argument("YALK test must contain at least one point");
    }
    for (const auto& point : config.points) {
        if (!isFinite(point.commandVolts)
            || point.commandVolts < 0.0
            || point.commandVolts > config.fullScaleVolts) {
            throw std::invalid_argument("YALK command voltage is outside the configured range");
        }
    }
}

} // namespace

YalkAnalogProcedure::YalkAnalogProcedure(YalkAnalogProcedureConfig config)
    : config_(std::move(config))
{
    validateConfig(config_);
}

double YalkAnalogProcedure::codeToVolts(
    double rawCode,
    const YalkCalibration& calibration,
    double fullScaleVolts)
{
    if (!isFinite(rawCode)
        || !isFinite(calibration.zeroCode)
        || !isFinite(calibration.fullScaleCode)
        || !isFinite(fullScaleVolts)
        || fullScaleVolts <= 0.0
        || calibration.fullScaleCode <= calibration.zeroCode) {
        throw std::invalid_argument("Invalid YALK calibration or raw code");
    }

    return (rawCode - calibration.zeroCode)
        * fullScaleVolts
        / (calibration.fullScaleCode - calibration.zeroCode);
}

YalkProcedureResult YalkAnalogProcedure::execute(
    unsigned channel,
    IIsdRouter& isd,
    IVoltageSource& source,
    IReferenceVoltmeter& voltmeter,
    IYalkReader& yalk,
    IProcedureWaiter& waiter) const
{
    YalkProcedureResult result;
    result.channel = channel;

    if (channel == 0) {
        result.message = "YALK channel numbers start at one";
        return result;
    }

    bool channelWasConnected = false;
    bool sourceWasEnabled = false;
    try {
        isd.reset();
        result.calibration = yalk.readCalibration(channel);

        // Validate calibration before enabling a physical output.
        (void)codeToVolts(
            result.calibration.zeroCode,
            result.calibration,
            config_.fullScaleVolts);

        const double allowedErrorVolts = config_.fullScaleVolts
            * config_.allowedErrorPercentFullScale / 100.0;
        result.verdict = Verdict::Ok;

        for (const auto& point : config_.points) {
            YalkPointResult pointResult;
            pointResult.commandVolts = point.commandVolts;
            pointResult.expectedSignal = point.expectedSignal;

            source.setVoltage(point.commandVolts);
            if (!sourceWasEnabled) {
                source.outputOn();
                sourceWasEnabled = true;
            }
            if (!channelWasConnected) {
                isd.connectChannel(channel);
                channelWasConnected = true;
            }
            waiter.waitMilliseconds(config_.stabilizationMilliseconds);

            pointResult.referenceVolts = voltmeter.readVoltage();
            if (!isFinite(pointResult.referenceVolts)) {
                throw std::runtime_error("Reference voltmeter returned a non-finite value");
            }
            const auto codes = yalk.readRawCodes(channel, config_.sampleCount);
            if (codes.size() != config_.sampleCount
                || !std::all_of(codes.begin(), codes.end(), isFinite)) {
                throw std::runtime_error("YALK reader returned an invalid sample set");
            }

            pointResult.averageRawCode = std::accumulate(codes.begin(), codes.end(), 0.0)
                / static_cast<double>(codes.size());
            pointResult.measuredYalkVolts = codeToVolts(
                pointResult.averageRawCode,
                result.calibration,
                config_.fullScaleVolts);
            pointResult.absoluteErrorVolts = std::abs(
                pointResult.measuredYalkVolts - pointResult.referenceVolts);
            pointResult.errorPercentFullScale = pointResult.absoluteErrorVolts
                / config_.fullScaleVolts * 100.0;
            pointResult.lowerLimitVolts = pointResult.referenceVolts - allowedErrorVolts;
            pointResult.upperLimitVolts = pointResult.referenceVolts + allowedErrorVolts;

            bool signalMatches = true;
            if (point.expectedSignal.has_value()) {
                pointResult.actualSignal = yalk.readSignal(channel);
                signalMatches = pointResult.actualSignal == point.expectedSignal;
            }

            const bool analogMatches = pointResult.absoluteErrorVolts <= allowedErrorVolts;
            pointResult.verdict = analogMatches && signalMatches
                ? Verdict::Ok
                : Verdict::Fail;
            if (!analogMatches && !signalMatches) {
                pointResult.message = "Analog error and signal state are outside the allowed result";
            } else if (!analogMatches) {
                pointResult.message = "YALK voltage differs from the reference voltmeter";
            } else if (!signalMatches) {
                pointResult.message = "YALK signal state does not match the expected state";
            } else {
                pointResult.message = "OK";
            }

            if (pointResult.verdict != Verdict::Ok) {
                result.verdict = Verdict::Fail;
            }
            result.points.push_back(std::move(pointResult));
        }

        isd.disconnectChannel(channel);
        channelWasConnected = false;
        source.outputOff();
        sourceWasEnabled = false;
        result.message = result.verdict == Verdict::Ok
            ? "All YALK analog points passed"
            : "One or more YALK analog points failed";
    } catch (const std::exception& error) {
        result.verdict = Verdict::Error;
        result.message = error.what();
        if (channelWasConnected) {
            try {
                isd.disconnectChannel(channel);
            } catch (...) {
                result.message += "; failed to disable ISD channel";
            }
        }
        if (sourceWasEnabled) {
            try {
                source.outputOff();
            } catch (...) {
                result.message += "; failed to disable voltage source";
            }
        }
    }

    return result;
}

} // namespace orbita::stand
