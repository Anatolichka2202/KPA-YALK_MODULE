#include "orbita_stand/yalk_analog_procedure.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace orbita::stand;

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

double voltsToCode(double volts, const YalkCalibration& calibration)
{
    return calibration.zeroCode
        + volts / 6.2 * (calibration.fullScaleCode - calibration.zeroCode);
}

class FakeIsd final : public IIsdController {
public:
    void reset() override { resetCalled = true; }
    void setVoltage(unsigned channel, double volts) override
    {
        channels.push_back(channel);
        commands.push_back(volts);
    }
    void disable(unsigned channel) override
    {
        disabledChannel = channel;
        disableCalled = true;
    }

    bool resetCalled = false;
    bool disableCalled = false;
    unsigned disabledChannel = 0;
    std::vector<unsigned> channels;
    std::vector<double> commands;
};

class FakeVoltmeter final : public IReferenceVoltmeter {
public:
    explicit FakeVoltmeter(std::vector<double> readings)
        : readings_(std::move(readings)) {}

    double readVoltage() override
    {
        if (index_ >= readings_.size()) {
            throw std::runtime_error("No fake voltmeter reading");
        }
        return readings_[index_++];
    }

private:
    std::vector<double> readings_;
    std::size_t index_ = 0;
};

class FakeYalk final : public IYalkReader {
public:
    FakeYalk(
        YalkCalibration calibration,
        std::vector<double> measuredVolts,
        std::vector<bool> signals)
        : calibration_(calibration),
          measuredVolts_(std::move(measuredVolts)),
          signals_(std::move(signals)) {}

    YalkCalibration readCalibration(unsigned) override { return calibration_; }

    std::vector<double> readRawCodes(unsigned, std::size_t count) override
    {
        if (pointIndex_ >= measuredVolts_.size()) {
            throw std::runtime_error("No fake YALK point");
        }
        const double code = voltsToCode(measuredVolts_[pointIndex_++], calibration_);
        return std::vector<double>(count, code);
    }

    bool readSignal(unsigned) override
    {
        if (signalIndex_ >= signals_.size()) {
            throw std::runtime_error("No fake signal state");
        }
        return signals_[signalIndex_++];
    }

private:
    YalkCalibration calibration_;
    std::vector<double> measuredVolts_;
    std::vector<bool> signals_;
    std::size_t pointIndex_ = 0;
    std::size_t signalIndex_ = 0;
};

class FakeWaiter final : public IProcedureWaiter {
public:
    void waitMilliseconds(unsigned milliseconds) override { waits.push_back(milliseconds); }
    std::vector<unsigned> waits;
};

void referenceLikeRunPasses()
{
    const double reportVoltage = YalkAnalogProcedure::codeToVolts(
        522.7, {121.8, 922.0}, 6.2);
    require(std::abs(reportVoltage - 3.106) < 0.002,
            "linear conversion must reproduce the preserved KPA report");

    FakeIsd isd;
    FakeVoltmeter voltmeter({-0.020890, 3.107138, 6.220211});
    FakeYalk yalk(
        {121.8, 922.0},
        {-0.021, 3.106, 6.219},
        {false, true});
    FakeWaiter waiter;

    const auto result = YalkAnalogProcedure{}.execute(1, isd, voltmeter, yalk, waiter);

    require(result.verdict == Verdict::Ok, "reference-like run must pass");
    require(result.points.size() == 3, "three voltage points are required");
    require(isd.resetCalled, "ISD reset is required");
    require(isd.commands == std::vector<double>({0.0, 3.1, 6.2}), "wrong ISD points");
    require(waiter.waits == std::vector<unsigned>({150, 150, 150}), "wrong stabilization waits");
    require(isd.disableCalled && isd.disabledChannel == 1, "ISD channel must be disabled");
    require(std::abs(result.points[1].lowerLimitVolts - 3.076138) < 1e-9,
            "0.5 percent of 6.2 V must be 0.031 V");
}

void excessiveFullScaleErrorFails()
{
    FakeIsd isd;
    FakeVoltmeter voltmeter({0.0, 3.1, 6.2});
    FakeYalk yalk({121.8, 922.0}, {0.0, 3.14, 6.2}, {false, true});
    FakeWaiter waiter;

    const auto result = YalkAnalogProcedure{}.execute(7, isd, voltmeter, yalk, waiter);

    require(result.verdict == Verdict::Fail, "40 mV error must exceed the 31 mV limit");
    require(result.points[1].verdict == Verdict::Fail, "middle point must fail");
    require(isd.disableCalled, "ISD must be disabled after a failed verdict");
}

void equipmentErrorStillDisablesIsd()
{
    FakeIsd isd;
    FakeVoltmeter voltmeter({0.0});
    FakeYalk yalk({121.8, 922.0}, {}, {});
    FakeWaiter waiter;

    const auto result = YalkAnalogProcedure{}.execute(2, isd, voltmeter, yalk, waiter);

    require(result.verdict == Verdict::Error, "equipment failure must produce ERROR");
    require(isd.disableCalled, "ISD must be disabled after an equipment error");
}

} // namespace

int main()
{
    try {
        referenceLikeRunPasses();
        excessiveFullScaleErrorFails();
        equipmentErrorStillDisablesIsd();
        std::cout << "YALK analog procedure tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "YALK analog procedure test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
