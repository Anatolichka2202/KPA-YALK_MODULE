#include "hardware/stand_config.h"
#include "hardware/stand_hardware.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

std::vector<unsigned> yalkAddresses()
{
    std::vector<unsigned> result;
    for (unsigned address = 1; address <= 28; ++address) result.push_back(address);
    for (unsigned address = 32; address <= 43; ++address) result.push_back(address);
    for (unsigned address = 45; address <= 70; ++address) result.push_back(address);
    for (unsigned address = 74; address <= 87; ++address) result.push_back(address);
    return result;
}

unsigned staircaseCode(unsigned channel)
{
    return channel <= 10 ? 780u + (channel - 1) * 30u
                         : 1800u + (channel - 11) * 20u;
}

void pause(unsigned milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: yalk_overload_probe <stand.yaml> [log-file]\n";
        return 2;
    }

    std::ofstream log;
    if (argc == 3) log.open(argv[2], std::ios::out | std::ios::trunc);
    const auto write = [&log](const std::string& line) {
        std::cout << line << std::endl;
        if (log) { log << line << '\n'; log.flush(); }
    };

    // Deliberately limited commissioning probe: 4 addresses x (+12 V, -12 V).
    // The production procedure remains 80 x 2 with its configured timing.
    const std::array<unsigned, 4> stressed{1, 25, 50, 87};
    constexpr unsigned sampleCount = 4;
    constexpr unsigned baselineSettleMs = 300;
    constexpr unsigned overloadSettleMs = 1000;
    constexpr unsigned cleanupSettleMs = 300;
    constexpr double maximumDeltaCode = 5.0;
    const auto observed = yalkAddresses();

    tu::hardware::StandHardware stand(tu::hardware::loadStandConfig(argv[1]));
    bool passed = true;
    try {
        write("START: four addresses, eight ±12 V impacts, 1 s settle, criterion ±5 codes");
        stand.supply().setCurrentLimit(0.6);
        stand.supply().setVoltage(27.0);
        stand.supply().setOutput(true);
        pause(500);

        stand.isd().setSwitch(3, 95, false);
        stand.isd().setSwitch(3, 96, false);
        for (const unsigned channel : observed)
            stand.isd().setAnalog(channel, staircaseCode(channel), true);

        if (!stand.yalk().startYalk(std::chrono::milliseconds(500),
                                    std::chrono::milliseconds(3000), {}))
            throw std::runtime_error("reference204 not received");
        pause(baselineSettleMs);
        const auto baseline = stand.yalk().readYalkSnapshot(
            sampleCount, std::chrono::milliseconds(3000), {});

        for (const auto [common, sign] : std::array<std::pair<unsigned, const char*>, 2>{
                 {{96, "+12"}, {95, "-12"}}}) {
            for (const unsigned target : stressed) {
                stand.isd().setSwitch(3, common, true);
                stand.isd().setAnalog(target, 0, false);
                stand.isd().setSwitch(3, target, true);
                pause(overloadSettleMs);

                const auto current = stand.yalk().readYalkSnapshot(
                    sampleCount, std::chrono::milliseconds(3000), {});
                double maximum = 0.0;
                unsigned failed = 0;
                for (const unsigned address : observed) {
                    if (address == target) continue;
                    const double delta = current.at(address - 1).codeMean
                        - baseline.at(address - 1).codeMean;
                    maximum = std::max(maximum, std::abs(delta));
                    if (std::abs(delta) > maximumDeltaCode) ++failed;
                }
                write(std::string("impact=") + sign + "V"
                    + " target=" + std::to_string(target)
                    + " max_abs_delta_code=" + std::to_string(maximum)
                    + " failed_neighbors=" + std::to_string(failed)
                    + " criterion=" + std::to_string(maximumDeltaCode));
                passed = passed && failed == 0;

                stand.isd().setSwitch(3, target, false);
                stand.isd().setSwitch(3, common, false);
                stand.isd().setAnalog(target, staircaseCode(target), true);
                pause(cleanupSettleMs);
            }
        }

        stand.isd().setSwitch(3, 95, false);
        stand.isd().setSwitch(3, 96, false);
        for (auto channel = observed.rbegin(); channel != observed.rend(); ++channel)
            stand.isd().setAnalog(*channel, 0, false);
        stand.yalk().stop();
        stand.supply().safeOff();
        write(std::string("DONE: ") + (passed ? "NORMA" : "NE NORMA"));
        return passed ? 0 : 1;
    } catch (const std::exception& error) {
        stand.safeStop();
        write(std::string("ERROR: ") + error.what());
        return 3;
    } catch (...) {
        stand.safeStop();
        write("ERROR: unknown exception");
        return 3;
    }
}
