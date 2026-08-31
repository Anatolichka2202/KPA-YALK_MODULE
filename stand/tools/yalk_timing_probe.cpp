#include "orbita_stand/equipment_adapters.h"
#include "orbita_stand/ulk_udp_transport.h"
#include "orbita_stand/v7_visa_voltmeter.h"
#include "orbita_stand/yalk_frame.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

unsigned number(const char* value, const char* name, unsigned maximum)
{
    std::size_t parsed = 0;
    const auto result = std::stoul(value, &parsed, 0);
    if (parsed != std::string(value).size() || result > maximum) {
        throw std::invalid_argument(std::string("Invalid ") + name);
    }
    return static_cast<unsigned>(result);
}

struct ChannelReading {
    std::uint16_t raw = 0;
    double analogMean = 0.0;
    unsigned analogMedian = 0;
    bool signal = false;
    std::uint64_t firstSequence = 0;
    std::uint64_t lastSequence = 0;
    std::array<double, 100> allAnalogMean{};
    std::array<bool, 100> allSignal{};
    std::array<std::uint16_t, 100> allRawLast{};
};

ChannelReading readFresh(orbita::stand::UlkUdpTransport& adapter,
                         unsigned address, unsigned count,
                         std::uint64_t afterSequence)
{
    std::vector<unsigned> analog;
    analog.reserve(count);
    std::uint64_t codeSum = 0;
    unsigned signalOnes = 0;
    std::array<std::uint64_t, 100> allCodeSums{};
    std::array<unsigned, 100> allSignalOnes{};
    ChannelReading result;
    for (unsigned index = 0; index < count; ++index) {
        const auto frame = adapter.waitFrame(
            orbita::stand::UlkFrameKind::Reference204, afterSequence,
            std::chrono::milliseconds(3000));
        afterSequence = frame.sequence;
        if (!result.firstSequence) result.firstSequence = frame.sequence;
        result.lastSequence = frame.sequence;
        const auto words = orbita::stand::decodeYalkReferenceFrame(frame.payload);
        for (std::size_t word = 0; word < words.size(); ++word) {
            allCodeSums[word] += words[word].analogCode;
            if (words[word].contact) ++allSignalOnes[word];
            result.allRawLast[word] = words[word].rawWord;
        }
        const auto& sample = words.at(address - 1);
        result.raw = sample.rawWord;
        codeSum += sample.analogCode;
        analog.push_back(sample.analogCode);
        if (sample.contact) ++signalOnes;
    }
    std::sort(analog.begin(), analog.end());
    result.analogMean = static_cast<double>(codeSum) / count;
    result.analogMedian = analog[analog.size() / 2];
    result.signal = signalOnes * 2 >= count;
    for (std::size_t word = 0; word < result.allAnalogMean.size(); ++word) {
        result.allAnalogMean[word] = static_cast<double>(allCodeSums[word]) / count;
        result.allSignal[word] = allSignalOnes[word] * 2 >= count;
    }
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 5) {
        std::cerr
            << "Usage: yalk_timing_probe <isd-ip> <adapter-ip> <local-ip> <isd-channel>\n"
               "Runs production ROKT YALK for 0.00/3.10/6.20 V, V7 and selected ULK address samples.\n";
        return 2;
    }

    const unsigned isdChannel = number(argv[4], "ISD channel", 255);
    orbita::stand::IsdHttpRouter isd({argv[1], 80, 3000, 2, {}});
    orbita::stand::UlkUdpTransport adapter({argv[2], argv[3], 1113, 800, 4096});
    bool isdPrepared = false;

    try {
        orbita::stand::V7VisaVoltmeter meter;
        std::cout << "TARGET isd=" << argv[1]
                  << " adapter=" << argv[2]
                  << " local=" << argv[3]
                  << " channel=" << isdChannel
                  << " v7=" << meter.resourceName() << '\n';

        isd.prepareYalk();
        isdPrepared = true;
        adapter.prepareYalkReference();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        isd.prepareYalk();
        adapter.startPreparedYalkReference();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        const auto calibrationAfter = adapter.stats().lastSequence;
        const auto zero = readFresh(adapter, 97, 16, calibrationAfter);
        const auto full = readFresh(adapter, 99, 16, zero.lastSequence);
        std::cout << "CALIBRATION addr97=" << zero.analogMedian
                  << " addr99=" << full.analogMedian
                  << " addr97_raw=" << zero.raw
                  << " addr99_raw=" << full.raw << '\n';
        if (zero.analogMedian < 100 || zero.analogMedian > 150
            || full.analogMedian < 890 || full.analogMedian > 950) {
            throw std::runtime_error(
                "YALK calibration is outside verified addr97~125/addr99~922 ranges");
        }

        constexpr std::array<double, 3> points{0.00, 3.10, 6.20};
        std::array<ChannelReading, 3> readings;
        std::array<double, 3> v7Readings{};
        std::array<bool, 3> pointOk{};
        for (std::size_t point = 0; point < points.size(); ++point) {
            const double commandVolts = points[point];
            isd.setYalkVoltage(isdChannel, commandVolts);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            const auto afterSettling = adapter.stats().lastSequence;
            v7Readings[point] = meter.readVoltage();
            readings[point] = readFresh(adapter, isdChannel, 16, afterSettling);
            const auto& yalk = readings[point];
            const double yalkVolts =
                (yalk.analogMean - zero.analogMean) * 6.2
                / (full.analogMean - zero.analogMean);
            const double absoluteError = std::abs(yalkVolts - v7Readings[point]);
            const double reducedError = absoluteError / 6.2 * 100.0;
            const bool signalOk = point == 0 ? !yalk.signal
                : point == 2 ? yalk.signal : true;
            pointOk[point] = reducedError <= 0.5 && signalOk;
            std::cout << std::fixed << std::setprecision(6)
                      << "POINT command_v=" << commandVolts
                      << " raw=" << yalk.raw
                      << " analog_mean=" << yalk.analogMean
                      << " analog_median=" << yalk.analogMedian
                      << " signal=" << (yalk.signal ? 1 : 0)
                      << " v7=" << v7Readings[point]
                      << " yalk_v=" << yalkVolts
                      << " absolute_error_v=" << absoluteError
                      << " reduced_error_percent=" << reducedError
                      << " tolerance_percent=0.500000"
                      << " point_ok=" << (pointOk[point] ? 1 : 0)
                      << " samples=16"
                      << " first_sequence=" << yalk.firstSequence
                      << " last_sequence=" << yalk.lastSequence << '\n';
        }

        unsigned candidates = 0;
        for (unsigned address = 1; address <= 100; ++address) {
            const auto index = address - 1;
            const auto [minimum, maximum] = std::minmax({
                readings[0].allAnalogMean[index],
                readings[1].allAnalogMean[index],
                readings[2].allAnalogMean[index]});
            const bool signalChanged = readings[0].allSignal[index] != readings[1].allSignal[index]
                || readings[0].allSignal[index] != readings[2].allSignal[index];
            if (maximum - minimum >= 10.0 || signalChanged) {
                ++candidates;
                std::cout << "CANDIDATE address=" << address
                          << " analog_0=" << readings[0].allAnalogMean[index]
                          << " analog_3_1=" << readings[1].allAnalogMean[index]
                          << " analog_6_2=" << readings[2].allAnalogMean[index]
                          << " signal_0=" << readings[0].allSignal[index]
                          << " signal_3_1=" << readings[1].allSignal[index]
                          << " signal_6_2=" << readings[2].allSignal[index]
                          << " raw_6_2=" << readings[2].allRawLast[index] << '\n';
            }
        }

        const double selectedMinimum = std::min({
            readings[0].analogMean, readings[1].analogMean, readings[2].analogMean});
        const double selectedMaximum = std::max({
            readings[0].analogMean, readings[1].analogMean, readings[2].analogMean});
        const bool selectedResponded = selectedMaximum - selectedMinimum >= 10.0
            || readings[0].signal != readings[1].signal
            || readings[0].signal != readings[2].signal;
        const bool verdictOk = selectedResponded
            && std::all_of(pointOk.begin(), pointOk.end(), [](bool value) { return value; });

        isd.disableYalkOutput(isdChannel);
        isd.reset();
        isdPrepared = false;
        adapter.stop();
        std::cout << "RESULT " << (verdictOk ? "DIAGNOSTIC_OK" : "DIAGNOSTIC_FAIL")
                  << " selected_address=" << isdChannel
                  << " candidates=" << candidates
                  << " cleanup=complete\n";
        return verdictOk ? 0 : 4;
    } catch (const std::exception& error) {
        if (isdPrepared) {
            try { isd.disableYalkOutput(isdChannel); } catch (...) {}
        }
        try { isd.reset(); } catch (...) {}
        adapter.stop();
        std::cerr << "ERROR " << error.what() << "\nCLEANUP attempted=true\n";
        return 1;
    }
}
