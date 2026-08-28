#include "orbita_stand/equipment_adapters.h"
#include "orbita_stand/v7_visa_voltmeter.h"

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
    if (argc != 6) {
        std::cerr << "Usage: yalk_timing_probe <isd-ip> <isd-channel> <adapter-ip> <local-ip> <seconds-per-point>\n"
                     "Sets 0, 3 and 6 V through the ISD analog channel, reads V7-78/1 and YALK mode 6.\n";
        return 2;
    }

    const auto isdChannel = number(argv[2], "ISD channel", 255);
    const auto secondsPerPoint = number(argv[5], "duration", 60);
    orbita::stand::IsdHttpRouter isd({argv[1], 80, 1500, 2, {}});
    orbita::stand::UbsiUdpAdapter adapter({argv[3], argv[4], 1001, 1101, 100});
    orbita::stand::V7VisaVoltmeter meter;

    // Из Delphi: 4095 соответствует 10 В, поэтому 3 и 6 В представлены
    // ближайшими целыми кодами. Фактическое значение подтверждается В7.
    constexpr std::array<double, 3> points{0.0, 3.0, 6.0};
    constexpr double codesPerVolt = 409.5;

    try {
        std::cout << "TARGET isd=" << argv[1] << " channel=" << isdChannel
                  << " adapter=" << argv[3] << " v7=" << meter.resourceName() << '\n';
        std::cout << "SELECT adapter_mode=6(YALK)\n";
        adapter.selectMode(6);

        std::vector<std::uint16_t> previousWords;
        try { previousWords = orbita::stand::UbsiUdpAdapter::decodeYalkPacket(
            adapter.receiveRawPacket(), 0xFFFF); }
        catch (const std::exception& error) {
            std::cout << "BASELINE_ADAPTER unavailable=" << error.what() << '\n';
        }

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
                    const auto packet = adapter.receiveRawPacket();
                    const auto now = millisecondsSince(start);
                    if (!adapterFirst) adapterFirst = now;
                    const auto words = orbita::stand::UbsiUdpAdapter::decodeYalkPacket(packet, 0xFFFF);
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
        std::cout << "SAFE_OFF isd_channel=" << isdChannel << '\n';
        return 0;
    } catch (const std::exception& error) {
        try { isd.setAnalog(isdChannel, 0, false); } catch (...) {}
        std::cerr << "ERROR " << error.what() << "\nSAFE_OFF attempted=true\n";
        return 1;
    }
}
