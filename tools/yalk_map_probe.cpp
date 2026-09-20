#include "hardware/stand_config.h"
#include "hardware/stand_hardware.h"

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using tu::hardware::YalkChannelReading;

const std::vector<unsigned> kWorkingAddresses{
    1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,
    32,33,34,35,36,37,38,39,40,41,42,43,
    45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,
    74,75,76,77,78,79,80,81,82,83,84,85,86,87
};

// Это только безопасный диагностический набор, уже используемый стендом для ЯЛК.
// 29/30/31/44/71/72/73/88 исключены как отдельные подтверждённые маршруты ЯВП;
// 89..96 сюда также не входят. Probe не использует type=3 и не подаёт ±12 В.
const std::vector<unsigned>& kPhysicalCandidates = kWorkingAddresses;

void waitMs(unsigned value)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(value));
}

double voltsFromCode(double code, double zeroCode, double fullCode)
{
    return (code - zeroCode) * 6.2 / (fullCode - zeroCode);
}

struct Candidate {
    unsigned logical = 0;
    double delta = 0.0;
    double volts = 0.0;
};

struct MapRow {
    unsigned physical = 0;
    unsigned logical = 0;
    double bestDelta = 0.0;
    double secondDelta = 0.0;
    double yalkVolts = 0.0;
    double v7Volts = 0.0;
    bool confident = false;
};

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    try {
        const std::filesystem::path configPath = argc >= 2
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("data/stand_ktma.yaml");

        auto config = tu::hardware::loadStandConfig(configPath);
        tu::hardware::StandHardware stand(config);

        std::cout << "YALK_MAP_PROBE_BEGIN\n";
        std::cout << "mode=safe_3.1V_only\n";
        std::cout << "physical_candidate_count=" << kPhysicalCandidates.size() << "\n";
        std::cout << "logical_address_count=" << kWorkingAddresses.size() << "\n";
        std::cout << "forbidden=type4,type7,type3,+12V,-12V\n";

        std::cout << "SUPPLY_IDN=" << stand.probeSupplyCold() << "\n";
        std::cout << "ISD_PROBE=" << stand.probeIsd() << "\n";
        std::cout << "V7_IDN=" << stand.probeV7() << "\n";

        stand.supply().setCurrentLimit(0.6);
        stand.supply().setVoltage(27.0);
        stand.supply().setOutput(true);
        waitMs(1000);

        if (!stand.yalk().startYalk(std::chrono::milliseconds(500),
                                    std::chrono::milliseconds(3000), [] {})) {
            throw std::runtime_error("Не получен reference204 после запуска ЯЛК");
        }

        const auto calibration = stand.yalk().readYalkSnapshot(
            16, std::chrono::milliseconds(3000), [] {});
        if (calibration.size() < 100)
            throw std::runtime_error("reference204 содержит меньше 100 слов");

        const double zeroCode = calibration.at(96).codeMean; // address 97
        const double fullCode = calibration.at(98).codeMean; // address 99
        const double span = fullCode - zeroCode;
        if (!(span > 100.0))
            throw std::runtime_error("Недостоверная калибровка 97/99: span=" + std::to_string(span));

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "CAL zero97=" << zeroCode << " full99=" << fullCode
                  << " span=" << span << "\n";
        std::cout << "CSV physical,logical,best_delta,second_delta,yalk_v,v7_v,confident\n";

        std::vector<MapRow> rows;
        rows.reserve(kPhysicalCandidates.size());
        std::set<unsigned> assigned;

        for (const unsigned physical : kPhysicalCandidates) {
            const auto before = stand.yalk().readYalkSnapshot(
                4, std::chrono::milliseconds(3000), [] {});

            bool outputOn = false;
            try {
                stand.isd().setYalkVoltage(physical, 3.1);
                outputOn = true;
                waitMs(250);

                const double v7 = stand.v7().readDcVoltage();
                const auto after = stand.yalk().readYalkSnapshot(
                    8, std::chrono::milliseconds(3000), [] {});

                stand.isd().disableYalkOutput(physical);
                outputOn = false;
                waitMs(150);

                std::vector<Candidate> candidates;
                candidates.reserve(kWorkingAddresses.size());
                for (const unsigned logical : kWorkingAddresses) {
                    const double delta = std::abs(
                        after.at(logical - 1).codeMean - before.at(logical - 1).codeMean);
                    candidates.push_back({logical, delta,
                        voltsFromCode(after.at(logical - 1).codeMean, zeroCode, fullCode)});
                }
                std::sort(candidates.begin(), candidates.end(),
                    [](const Candidate& a, const Candidate& b) { return a.delta > b.delta; });

                const Candidate best = candidates.at(0);
                const Candidate second = candidates.at(1);

                // 3.1 В ~= половина шкалы. Порог намеренно широкий: probe должен
                // доказать однозначную трассу, а не выполнять метрологическую оценку.
                const bool stimulusPresent = std::isfinite(v7) && v7 > 2.5 && v7 < 3.7;
                const bool movedEnough = best.delta > span * 0.20;
                const bool separated = second.delta < best.delta * 0.60;
                const bool unique = !assigned.count(best.logical);
                const bool confident = stimulusPresent && movedEnough && separated && unique;

                rows.push_back({physical, best.logical, best.delta, second.delta,
                                best.volts, v7, confident});
                if (confident) assigned.insert(best.logical);

                std::cout << "MAP," << physical << ',' << best.logical << ','
                          << best.delta << ',' << second.delta << ','
                          << best.volts << ',' << v7 << ','
                          << (confident ? "YES" : "NO") << "\n";
            } catch (...) {
                if (outputOn) {
                    try { stand.isd().disableYalkOutput(physical); } catch (...) {}
                }
                throw;
            }
        }

        unsigned confidentCount = 0;
        for (const auto& row : rows) if (row.confident) ++confidentCount;

        std::cout << "SUMMARY confident=" << confidentCount
                  << " unique_logical=" << assigned.size()
                  << " expected=80\n";

        if (confidentCount == 80 && assigned.size() == 80) {
            std::cout << "RESULT=PASS_MAP_BIJECTION\n";
            std::cout << "YALK_MAP=";
            bool first = true;
            for (const auto& row : rows) {
                if (!first) std::cout << ';';
                first = false;
                std::cout << row.physical << ':' << row.logical;
            }
            std::cout << "\n";
            stand.safeStop();
            return 0;
        }

        std::cout << "RESULT=FAIL_OR_AMBIGUOUS_MAP\n";
        stand.safeStop();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "RESULT=ERROR\nERROR=" << error.what() << "\n";
        return 1;
    }
}
