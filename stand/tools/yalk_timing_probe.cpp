#include "orbita_stand/equipment_adapters.h"
#include "orbita_stand/ulk_udp_transport.h"
#include "orbita_stand/yalk_frame.h"
#include "orbita_stand/v7_visa_voltmeter.h"

#include <QCoreApplication>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

unsigned number(const char* value, const char* name, unsigned maximum)
{
    std::size_t parsed = 0;
    const auto result = std::stoul(value, &parsed, 0);
    if (parsed != std::string(value).size() || result > maximum) {
        throw std::invalid_argument(std::string("Invalid ") + name);
    }
    return static_cast<unsigned>(result);
}

long long millisecondsSince(const Clock::time_point& start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

std::optional<unsigned> firstChangedWord(const std::vector<std::uint16_t>& before,
                                         const std::vector<std::uint16_t>& after)
{
    if (before.size() != after.size()) return std::nullopt;
    for (std::size_t index = 0; index < before.size(); ++index) {
        if (before[index] != after[index]) return static_cast<unsigned>(index);
    }
    return std::nullopt;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 6) {
        std::cerr << "Usage: yalk_timing_probe <isd-ip> <isd-channel> <adapter-ip|passive> <local-ip> <seconds-per-point>\n"
                     "Sets 0, 3 and 6 V through the ISD analog channel, reads V7-78/1 and YALK mode 6.\n";
        return 2;
    }

    const auto isdChannel = number(argv[2], "ISD channel", 255);
    const auto secondsPerPoint = number(argv[5], "duration", 60);
    const bool passiveBroadcast = std::string(argv[3]) == "passive";
    orbita::stand::IsdHttpRouter isd({argv[1], 80, 1500, 2, {}});
    if (passiveBroadcast) throw std::invalid_argument("Для рабочего адаптера требуется IP, пассивный режим не поддерживается");
    orbita::stand::UlkUdpTransport adapter({argv[3], argv[4], 1113, 100, 4096});
    orbita::stand::V7VisaVoltmeter meter;

    // Из Delphi: 4095 соответствует 10 В, поэтому 3 и 6 В представлены
    // ближайшими целыми кодами. Фактическое значение подтверждается В7.
    constexpr std::array<double, 3> points{0.0, 3.0, 6.0};
    constexpr double codesPerVolt = 409.5;

    try {
        std::cout << "TARGET isd=" << argv[1] << " channel=" << isdChannel
                  << " adapter=" << argv[3] << " v7=" << meter.resourceName() << '\n';
        std::cout << "SELECT adapter_mode=6(YALK) port=1113\n";
        adapter.start(6);

        auto baselineFrame = adapter.waitFrame(orbita::stand::UlkFrameKind::Slow200, 0,
            std::chrono::milliseconds(3000));
        const auto& baselinePacket = baselineFrame.payload;
        if (baselinePacket.size() != 200) {
            throw std::runtime_error("Адаптер передал пакет не ЯЛК: ожидалось 200 байт, получено "
                + std::to_string(baselinePacket.size()));
        }
        std::vector<std::uint16_t> previousWords =
        {
            const auto samples = orbita::stand::decodeYalkSlowFrame(baselinePacket);
            for (const auto& sample : samples) previousWords.push_back(sample.rawWord);
        }
        std::cout << "BASELINE_ADAPTER bytes=200 words=100\n";

        for (const double volts : points) {
            const unsigned code = static_cast<unsigned>(std::lround(volts * codesPerVolt));
            const double v7Before = meter.readVoltage();
            const auto start = Clock::now();
            isd.setAnalog(isdChannel, code, true);
            std::cout << std::fixed << std::setprecision(6)
                      << "POINT volts=" << volts << " isd_code=" << code
                      << " t0_ms=0 v7_before=" << v7Before << '\n';

            std::optional<long long> v7First, v7Changed, adapterFirst, adapterChanged;
            double lastV7 = v7Before;
            const auto deadline = start + std::chrono::seconds(secondsPerPoint);
            while (Clock::now() < deadline) {
                try {
                    const auto packetFrame = adapter.waitFrame(orbita::stand::UlkFrameKind::Slow200,
                        baselineFrame.sequence, std::chrono::milliseconds(100));
                    baselineFrame = packetFrame;
                    const auto& packet = packetFrame.payload;
                    const auto now = millisecondsSince(start);
                    if (!adapterFirst) adapterFirst = now;
                    std::vector<std::uint16_t> words;
                    for (const auto& sample : orbita::stand::decodeYalkSlowFrame(packet)) words.push_back(sample.rawWord);
                    if (!adapterChanged && !previousWords.empty()) {
                        if (const auto word = firstChangedWord(previousWords, words)) {
                            adapterChanged = now;
                            std::cout << "ADAPTER_CHANGED t_ms=" << now
                                      << " word=" << *word
                                      << " before=" << previousWords[*word]
                                      << " after=" << words[*word] << '\n';
                        }
                    }
                    previousWords = words;
                } catch (const std::exception&) {
                    // No packet during this 100 ms poll; V7 is still sampled below.
                }

                const auto voltage = meter.readVoltage();
                const auto now = millisecondsSince(start);
                if (!v7First) v7First = now;
                if (!v7Changed && std::abs(voltage - v7Before) >= 0.02) {
                    v7Changed = now;
                    std::cout << "V7_CHANGED t_ms=" << now << " volts=" << voltage << '\n';
                }
                lastV7 = voltage;
            }
            std::cout << "RESULT volts=" << volts
                      << " v7_first_ms=" << (v7First ? std::to_string(*v7First) : "none")
                      << " v7_changed_ms=" << (v7Changed ? std::to_string(*v7Changed) : "none")
                      << " adapter_first_ms=" << (adapterFirst ? std::to_string(*adapterFirst) : "none")
                      << " adapter_changed_ms=" << (adapterChanged ? std::to_string(*adapterChanged) : "none")
                      << " v7_last=" << lastV7 << '\n';
        }
        isd.setAnalog(isdChannel, 0, false);
        adapter.stop();
        std::cout << "SAFE_OFF isd_channel=" << isdChannel << '\n';
        return 0;
    } catch (const std::exception& error) {
        try { isd.setAnalog(isdChannel, 0, false); } catch (...) {}
        std::cerr << "ERROR " << error.what() << "\nSAFE_OFF attempted=true\n";
        return 1;
    }
}
