#include "orbita_stand/equipment_adapters.h"
#include "orbita_stand/ulk_udp_transport.h"
#include "orbita_stand/v7_visa_voltmeter.h"
#include "orbita_stand/yalk_frame.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <chrono>
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
};

ChannelReading readFresh(orbita::stand::UlkUdpTransport& adapter,
                         unsigned address, unsigned count,
                         std::uint64_t afterSequence)
{
    std::vector<unsigned> analog;
    analog.reserve(count);
    std::uint64_t codeSum = 0;
    unsigned signalOnes = 0;
    ChannelReading result;
    for (unsigned index = 0; index < count; ++index) {
        const auto frame = adapter.waitFrame(
            orbita::stand::UlkFrameKind::Reference204, afterSequence,
            std::chrono::milliseconds(3000));
        afterSequence = frame.sequence;
        if (!result.firstSequence) result.firstSequence = frame.sequence;
        result.lastSequence = frame.sequence;
        const auto words = orbita::stand::decodeYalkReferenceFrame(frame.payload);
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
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 5) {
        std::cerr
            << "Usage: yalk_timing_probe <isd-ip> <adapter-ip> <local-ip> <isd-channel>\n"
               "Runs production ROKT YALK for 0.00/3.10/6.20 V, V7 and 16 fresh addr1 samples.\n";
        return 2;
    }

    const unsigned isdChannel = number(argv[4], "ISD channel", 255);
    orbita::stand::IsdHttpRouter isd({argv[1], 80, 1500, 2, {}});
    orbita::stand::UlkUdpTransport adapter({argv[2], argv[3], 1113, 800, 4096});
    bool isdPrepared = false;

    try {
        orbita::stand::V7VisaVoltmeter meter;
        std::cout << "TARGET isd=" << argv[1]
                  << " adapter=" << argv[2]
                  << " local=" << argv[3]
                  << " channel=" << isdChannel
                  << " v7=" << meter.resourceName() << '\n';

        adapter.startYalkReference();
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

        isd.prepareYalk();
        isdPrepared = true;
        constexpr std::array<double, 3> points{0.00, 3.10, 6.20};
        for (const double commandVolts : points) {
            isd.setYalkVoltage(isdChannel, commandVolts);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            const auto afterSettling = adapter.stats().lastSequence;
            const double v7 = meter.readVoltage();
            const auto yalk = readFresh(adapter, 1, 16, afterSettling);
            std::cout << std::fixed << std::setprecision(6)
                      << "POINT command_v=" << commandVolts
                      << " raw=" << yalk.raw
                      << " analog_mean=" << yalk.analogMean
                      << " analog_median=" << yalk.analogMedian
                      << " signal=" << (yalk.signal ? 1 : 0)
                      << " v7=" << v7
                      << " samples=16"
                      << " first_sequence=" << yalk.firstSequence
                      << " last_sequence=" << yalk.lastSequence << '\n';
        }

        isd.disableYalkOutput(isdChannel);
        isd.reset();
        isdPrepared = false;
        adapter.stop();
        std::cout << "RESULT OK cleanup=complete\n";
        return 0;
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
