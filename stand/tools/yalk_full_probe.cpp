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
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr unsigned SampleCount = 16;
constexpr auto SettleTime = std::chrono::milliseconds(150);
constexpr double FullScaleVolts = 6.2;
constexpr double ReducedTolerancePercent = 0.5;

struct AdapterReading {
    std::uint16_t rawLast = 0;
    double analogMean = 0.0;
    unsigned analogMedian = 0;
    bool signal = false;
    std::uint64_t firstSequence = 0;
    std::uint64_t lastSequence = 0;
};

struct PointReading {
    double commandVolts = 0.0;
    double v7Volts = 0.0;
    AdapterReading adapter;
};

std::vector<unsigned> workingAddresses()
{
    std::vector<unsigned> result;
    const auto append = [&result](unsigned first, unsigned last) {
        for (unsigned address = first; address <= last; ++address) {
            result.push_back(address);
        }
    };
    append(1, 28);
    append(32, 43);
    append(45, 70);
    append(74, 87);
    if (result.size() != 80) throw std::logic_error("YALK working address set must contain 80 entries");
    return result;
}

void requireReferenceHeader(const std::vector<std::uint8_t>& payload)
{
    if (payload.size() != 204
        || payload[0] != 0x02 || payload[1] != 0x00
        || payload[2] != 0x2B || payload[3] != 0x00) {
        throw std::runtime_error("Unexpected YALK Reference204 header");
    }
}

AdapterReading readFresh(orbita::stand::UlkUdpTransport& adapter,
                         unsigned address, std::uint64_t afterSequence)
{
    std::vector<unsigned> analog;
    analog.reserve(SampleCount);
    std::uint64_t codeSum = 0;
    unsigned signalOnes = 0;
    AdapterReading result;

    for (unsigned index = 0; index < SampleCount; ++index) {
        const auto frame = adapter.waitFrame(
            orbita::stand::UlkFrameKind::Reference204,
            afterSequence,
            std::chrono::milliseconds(3000));
        afterSequence = frame.sequence;
        requireReferenceHeader(frame.payload);
        const auto words = orbita::stand::decodeYalkReferenceFrame(frame.payload);
        const auto& sample = words.at(address - 1);
        if (!result.firstSequence) result.firstSequence = frame.sequence;
        result.lastSequence = frame.sequence;
        result.rawLast = sample.rawWord;
        codeSum += sample.analogCode;
        analog.push_back(sample.analogCode);
        if (sample.contact) ++signalOnes;
    }

    std::sort(analog.begin(), analog.end());
    result.analogMean = static_cast<double>(codeSum) / SampleCount;
    result.analogMedian = analog[analog.size() / 2];
    result.signal = signalOnes * 2 >= SampleCount;
    return result;
}

std::string timestamp()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%Y%m%d_%H%M%S");
    return out.str();
}

std::string optionalPercent(const std::optional<double>& value)
{
    if (!value) return {};
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << *value;
    return out.str();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        const auto addresses = workingAddresses();
        const bool addressSetOk = addresses.size() == 80
            && addresses.front() == 1 && addresses[27] == 28
            && addresses[28] == 32 && addresses[39] == 43
            && addresses[40] == 45 && addresses[65] == 70
            && addresses[66] == 74 && addresses.back() == 87;
        const double zero = 125.0;
        const double full = 922.0;
        const auto convert = [=](double code) {
            return (code - zero) * FullScaleVolts / (full - zero);
        };
        const bool conversionOk = std::abs(convert(zero)) < 1e-12
            && std::abs(convert((zero + full) / 2.0) - 3.1) < 1e-12
            && std::abs(convert(full) - 6.2) < 1e-12;
        if (!addressSetOk || !conversionOk) {
            std::cerr << "SELF_TEST FAIL addresses=" << addresses.size() << '\n';
            return 1;
        }
        std::cout << "SELF_TEST OK addresses=80 calibration=97/99 points=0/3.1/6.2\n";
        return 0;
    }
    if (argc != 4 && argc != 5) {
        std::cerr
            << "Usage: yalk_full_probe <isd-ip> <adapter-ip> <local-ip> [output-root]\n"
               "Diagnostic full YALK loop: 80 addresses x 0.00/3.10/6.20 V.\n";
        return 2;
    }

    const std::filesystem::path outputRoot = argc == 5 ? argv[4] : "runs/diagnostics";
    const std::filesystem::path runDirectory = outputRoot / ("yalk_full_" + timestamp());
    std::filesystem::create_directories(runDirectory);
    const auto csvPath = runDirectory / "channels.csv";
    const auto rawPath = runDirectory / "frames.ulkbin";
    std::ofstream csv(csvPath, std::ios::binary | std::ios::trunc);
    if (!csv) throw std::runtime_error("Cannot create YALK diagnostic CSV");
    csv << "address;isd_channel;command_v;v7_v;raw_last;analog_mean;analog_median;signal;"
           "yalk_v;absolute_error_v;reduced_error_percent;relative_error_percent;"
           "analog_ok;signal_ok;point_ok;first_sequence;last_sequence\n";

    orbita::stand::IsdHttpRouter isd({argv[1], 80, 1500, 2, {}});
    orbita::stand::UlkUdpTransport adapter({argv[2], argv[3], 1113, 800, 8192});
    bool isdPrepared = false;
    bool outputEnabled = false;
    unsigned activeChannel = 0;

    try {
        orbita::stand::V7VisaVoltmeter meter;
        std::cout << "DIAGNOSTIC_ONLY acceptance_ok=forbidden\n"
                  << "TARGET isd=" << argv[1]
                  << " adapter=" << argv[2]
                  << " local=" << argv[3]
                  << " v7=" << meter.resourceName()
                  << " output=" << runDirectory.string() << '\n';

        adapter.startRecord(rawPath.string());
        adapter.startYalkReference();
        const auto calibrationStart = adapter.stats().lastSequence;
        const auto zero = readFresh(adapter, 97, calibrationStart);
        const auto full = readFresh(adapter, 99, zero.lastSequence);
        std::cout << "CALIBRATION addr97_mean=" << zero.analogMean
                  << " addr97_median=" << zero.analogMedian
                  << " addr99_mean=" << full.analogMean
                  << " addr99_median=" << full.analogMedian << '\n';
        if (zero.analogMedian < 100 || zero.analogMedian > 150
            || full.analogMedian < 890 || full.analogMedian > 950
            || full.analogMean <= zero.analogMean) {
            throw std::runtime_error("Calibration 97/99 is outside verified ranges");
        }

        isd.prepareYalk();
        isdPrepared = true;
        unsigned failedPoints = 0;
        unsigned failedChannels = 0;
        unsigned completedChannels = 0;
        constexpr std::array<double, 3> commandPoints{0.00, 3.10, 6.20};

        for (const unsigned address : workingAddresses()) {
            activeChannel = address;
            std::array<PointReading, 3> readings;
            for (std::size_t point = 0; point < commandPoints.size(); ++point) {
                readings[point].commandVolts = commandPoints[point];
                isd.setYalkVoltage(activeChannel, commandPoints[point]);
                outputEnabled = true;
                std::this_thread::sleep_for(SettleTime);
                const auto afterSettling = adapter.stats().lastSequence;
                readings[point].v7Volts = meter.readVoltage();
                readings[point].adapter = readFresh(adapter, address, afterSettling);
            }

            const double calibrationVoltage = readings.back().v7Volts;
            if (!std::isfinite(calibrationVoltage) || calibrationVoltage < 5.5
                || calibrationVoltage > 6.5) {
                throw std::runtime_error("V7 full-scale point is outside safe 5.5..6.5 V range");
            }

            bool channelOk = true;
            for (std::size_t point = 0; point < readings.size(); ++point) {
                const auto& reading = readings[point];
                const double yalkVolts =
                    (reading.adapter.analogMean - zero.analogMean)
                    * calibrationVoltage / (full.analogMean - zero.analogMean);
                const double absoluteError = std::abs(yalkVolts - reading.v7Volts);
                const double reducedError = absoluteError / FullScaleVolts * 100.0;
                const std::optional<double> relativeError =
                    std::abs(reading.v7Volts) > 0.01
                    ? std::optional<double>{absoluteError / std::abs(reading.v7Volts) * 100.0}
                    : std::nullopt;
                const bool analogOk = reducedError <= ReducedTolerancePercent;
                const bool signalOk = point == 1
                    || (point == 0 ? !reading.adapter.signal : reading.adapter.signal);
                const bool pointOk = analogOk && signalOk;
                if (!pointOk) {
                    ++failedPoints;
                    channelOk = false;
                }

                csv << address << ';' << activeChannel << ';'
                    << std::fixed << std::setprecision(2) << reading.commandVolts << ';'
                    << std::setprecision(9) << reading.v7Volts << ';'
                    << reading.adapter.rawLast << ';'
                    << std::setprecision(6) << reading.adapter.analogMean << ';'
                    << reading.adapter.analogMedian << ';'
                    << (reading.adapter.signal ? 1 : 0) << ';'
                    << std::setprecision(9) << yalkVolts << ';'
                    << absoluteError << ';' << reducedError << ';'
                    << optionalPercent(relativeError) << ';'
                    << (analogOk ? 1 : 0) << ';' << (signalOk ? 1 : 0) << ';'
                    << (pointOk ? 1 : 0) << ';'
                    << reading.adapter.firstSequence << ';'
                    << reading.adapter.lastSequence << '\n';

                std::cout << "POINT address=" << address
                          << " channel=" << activeChannel
                          << " command_v=" << reading.commandVolts
                          << " v7=" << reading.v7Volts
                          << " raw=" << reading.adapter.rawLast
                          << " analog=" << reading.adapter.analogMean
                          << " signal=" << (reading.adapter.signal ? 1 : 0)
                          << " yalk_v=" << yalkVolts
                          << " reduced_error_percent=" << reducedError
                          << " result=" << (pointOk ? "OK" : "FAIL") << '\n';
            }
            csv.flush();
            if (!channelOk) ++failedChannels;
            ++completedChannels;
            isd.disableYalkOutput(activeChannel);
            outputEnabled = false;
            std::cout << "CHANNEL address=" << address
                      << " progress=" << completedChannels << "/80"
                      << " result=" << (channelOk ? "OK" : "FAIL") << '\n';
        }

        isd.reset();
        isdPrepared = false;
        adapter.stopRecord();
        adapter.stop();
        const auto stats = adapter.stats();
        std::cout << "SUMMARY channels=80 failed_channels=" << failedChannels
                  << " failed_points=" << failedPoints
                  << " reference204=" << stats.reference204
                  << " unknown=" << stats.unknown
                  << " dropped=" << stats.dropped
                  << " csv=" << csvPath.string()
                  << " raw=" << rawPath.string() << '\n';
        std::cout << (failedChannels ? "RESULT DIAGNOSTIC_FAIL\n"
                                     : "RESULT DIAGNOSTIC_OK\n");
        return failedChannels ? 4 : 0;
    } catch (const std::exception& error) {
        if (outputEnabled && activeChannel) {
            try { isd.disableYalkOutput(activeChannel); } catch (...) {}
        }
        if (isdPrepared) {
            try { isd.reset(); } catch (...) {}
        } else {
            try { isd.reset(); } catch (...) {}
        }
        adapter.stopRecord();
        adapter.stop();
        std::cerr << "ERROR " << error.what()
                  << "\nCLEANUP attempted=true"
                  << "\nPARTIAL_CSV " << csvPath.string()
                  << "\nRAW " << rawPath.string() << '\n';
        return 1;
    }
}
