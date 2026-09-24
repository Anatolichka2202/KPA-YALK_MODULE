#include "hardware/akip1160.h"
#include "hardware/bench_instruments.h"
#include "hardware/isd_router.h"
#include "hardware/stand_config.h"
#include "hardware/visa_instrument.h"
#include "hardware/yalk_reference_link.h"

#include <QCoreApplication>

#include <chrono>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct RoktSample {
    std::chrono::steady_clock::time_point time;
    std::uint64_t sequence = 0;
    double code44 = 0.0;
    double code97 = 0.0;
    double code99 = 0.0;
};

void printRoktSamples(unsigned frequency, const std::vector<RoktSample>& samples)
{
    if (samples.empty()) throw std::runtime_error("ROKT: no reference204 samples");
    double minCode = samples.front().code44;
    double maxCode = minCode;
    for (const auto& sample : samples) {
        minCode = std::min(minCode, sample.code44);
        maxCode = std::max(maxCode, sample.code44);
        const double scale = sample.code99 - sample.code97;
        const double volts = std::abs(scale) > 1.0
            ? (sample.code44 - sample.code97) * 6.2 / scale : 0.0;
        std::cout << "ROKT_SAMPLE frequency_hz=" << frequency
                  << " sequence=" << sample.sequence
                  << " time_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(
                      sample.time - samples.front().time).count()
                  << " code44=" << sample.code44
                  << " code97=" << sample.code97
                  << " code99=" << sample.code99
                  << " calibrated_v=" << volts << '\n';
    }
    const auto duration = std::chrono::duration<double>(
        samples.back().time - samples.front().time).count();
    std::cout << "ROKT_SUMMARY frequency_hz=" << frequency
              << " samples=" << samples.size()
              << " duration_s=" << duration
              << " code44_min=" << minCode
              << " code44_max=" << maxCode
              << " code44_span=" << maxCode - minCode << std::endl;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2 && !(argc == 3 && (std::string(argv[2]) == "--compare-meas"
                                        || std::string(argv[2]) == "--production-first"
                                        || std::string(argv[2]) == "--rokt-observe"))) {
        std::cerr << "Usage: yvp_band_probe <stand.yaml> [--compare-meas|--production-first|--rokt-observe]\n";
        return 2;
    }
    const bool compareMeas = argc == 3 && std::string(argv[2]) == "--compare-meas";
    const bool productionFirst = argc == 3 && std::string(argv[2]) == "--production-first";
    const bool roktObserve = argc == 3 && std::string(argv[2]) == "--rokt-observe";

    bool ownPower = false;
    bool inputMayBeOn = false;
    bool measureMayBeOn = false;
    bool gainMayBeOn = false;
    bool cleanupOk = true;
    try {
        const auto config = tu::hardware::loadStandConfig(argv[1]);
        tu::hardware::Akip1160 supply(config.supply);
        tu::hardware::RigolGenerator rigol(config.generator);
        tu::hardware::IsdRouter isd(config.isd);
        std::unique_ptr<tu::hardware::YalkReferenceLink> rokt;
        std::mutex roktMutex;
        std::vector<RoktSample> roktSamples;
        std::unique_ptr<tu::hardware::VisaInstrument> meter;
        if (!productionFirst)
            meter = std::make_unique<tu::hardware::VisaInstrument>(
                tu::hardware::VisaConfig{config.v7.resourceExpressions, 25000});

        const auto cleanup = [&] {
            if (rokt) rokt->stop();
            try { rigol.safeOff(); std::cout << "CLEANUP RIGOL OFF OK\n"; }
            catch (const std::exception& e) {
                cleanupOk = false;
                std::cerr << "CLEANUP RIGOL OFF ERROR " << e.what() << '\n';
            }
            if (gainMayBeOn) {
                try { isd.setSwitch(2, 2, false); std::cout << "CLEANUP KU2 OFF OK\n"; }
                catch (const std::exception& e) {
                    cleanupOk = false;
                    std::cerr << "CLEANUP KU2 OFF ERROR " << e.what() << '\n';
                }
            }
            if (measureMayBeOn) {
                try { isd.setSwitch(3, 44, false); std::cout << "CLEANUP V7 ROUTE OFF OK\n"; }
                catch (const std::exception& e) {
                    cleanupOk = false;
                    std::cerr << "CLEANUP V7 ROUTE OFF ERROR " << e.what() << '\n';
                }
            }
            if (inputMayBeOn) {
                try { isd.setSwitch(2, 33, false); std::cout << "CLEANUP INPUT OFF OK\n"; }
                catch (const std::exception& e) {
                    cleanupOk = false;
                    std::cerr << "CLEANUP INPUT OFF ERROR " << e.what() << '\n';
                }
            }
            if (ownPower) {
                try {
                    supply.safeOff();
                    const auto off = supply.readState();
                    std::cout << "CLEANUP AKIP output=" << off.outputEnabled
                              << " voltage=" << off.measuredVoltageV << '\n';
                } catch (const std::exception& e) {
                    cleanupOk = false;
                    std::cerr << "CLEANUP AKIP OFF ERROR " << e.what() << '\n';
                }
            }
        };

        try {
            const auto start = supply.readState();
            std::cout << "START AKIP output=" << start.outputEnabled
                      << " voltage=" << start.measuredVoltageV << '\n';
            if (!start.outputEnabled) {
                supply.setCurrentLimit(0.6);
                supply.setVoltage(27.0);
                ownPower = true;
                supply.setOutput(true);
                std::this_thread::sleep_for(std::chrono::seconds(30));
            }
            const auto powered = supply.readState();
            if (!powered.outputEnabled || powered.measuredVoltageV < 26.5
                || powered.measuredVoltageV > 27.5)
                throw std::runtime_error("27 V not confirmed");
            std::cout << "POWER output=" << powered.outputEnabled
                      << " voltage=" << powered.measuredVoltageV
                      << " current=" << powered.measuredCurrentA << '\n';

            if (roktObserve) {
                rokt = std::make_unique<tu::hardware::YalkReferenceLink>(config.yalk);
                rokt->setLiveYalkSink([&](const auto& frame, std::uint64_t sequence) {
                    if (frame.size() < 99) return;
                    std::lock_guard<std::mutex> lock(roktMutex);
                    if (roktSamples.size() < 2000)
                        roktSamples.push_back({std::chrono::steady_clock::now(), sequence,
                                               frame[43].codeMean, frame[96].codeMean,
                                               frame[98].codeMean});
                });
                if (!rokt->startYalk(std::chrono::milliseconds(100),
                                     std::chrono::seconds(10), {}))
                    throw std::runtime_error("ROKT: reference204 not ready");
                std::cout << "ROKT reference204 READY address=44 calibration=97,99" << std::endl;
            }

            rigol.safeOff();
            isd.setAnalog(44, 0, false);
            isd.setSwitch(2, 33, false);
            isd.setSwitch(3, 44, false);
            isd.setSwitch(2, 2, false);
            inputMayBeOn = true;
            isd.setSwitch(2, 33, true);
            measureMayBeOn = true;
            isd.setSwitch(3, 44, true);
            if (!productionFirst) {
                gainMayBeOn = true;
                isd.setSwitch(2, 2, true);
            }
            std::cout << "ROUTE input=type2/33 measurement=type3/44 KU2="
                      << (productionFirst ? "OFF" : "type2/2 ON") << " ACK\n";

            if (roktObserve) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                std::vector<RoktSample> baseline;
                {
                    std::lock_guard<std::mutex> lock(roktMutex);
                    baseline = roktSamples;
                    roktSamples.clear();
                }
                std::cout << "BASELINE RIGOL OFF" << std::endl;
                printRoktSamples(0, baseline);
            }

            if (productionFirst) {
                meter = std::make_unique<tu::hardware::VisaInstrument>(
                    tu::hardware::VisaConfig{config.v7.resourceExpressions, 5000});
                std::cout << "V7=" << meter->resourceName()
                          << " timeout_ms=5000" << std::endl;
                std::cout << "V7_DC_BEFORE=" << meter->query("MEAS:VOLT:DC?") << std::endl;
                rigol.setSine(1, 500, 8.0, 0.0);
                rigol.output(1, true);
                std::this_thread::sleep_for(std::chrono::seconds(2));
                for (unsigned attempt = 1; attempt <= 3; ++attempt) {
                    const auto started = std::chrono::steady_clock::now();
                    try {
                        const auto answer = meter->query("MEAS:VOLT:AC?");
                        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started).count();
                        std::cout << "PRODUCTION_FIRST attempt=" << attempt
                                  << " timeout_ms=5000 latency_ms=" << elapsed
                                  << " v7_vrms=" << answer << std::endl;
                        break;
                    } catch (const std::exception& error) {
                        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started).count();
                        std::cout << "PRODUCTION_FIRST attempt=" << attempt
                                  << " timeout_ms=5000 latency_ms=" << elapsed
                                  << " error=" << error.what() << std::endl;
                        if (attempt < 3) {
                            meter->reconnect();
                            std::this_thread::sleep_for(std::chrono::milliseconds(250));
                        }
                    }
                }
                meter.reset();
                meter = std::make_unique<tu::hardware::VisaInstrument>(
                    tu::hardware::VisaConfig{config.v7.resourceExpressions, 25000});
                const auto started = std::chrono::steady_clock::now();
                const auto answer = meter->query("MEAS:VOLT:AC?");
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                std::cout << "PRODUCTION_FIRST timeout_ms=25000 latency_ms=" << elapsed
                          << " v7_vrms=" << answer << std::endl;
                cleanup();
                return cleanupOk ? 0 : 4;
            }

            meter->write("CONF:VOLT:AC");
            std::cout << "V7=" << meter->resourceName() << '\n';
            for (const unsigned frequency : {5u, 10u, 20u, 500u}) {
                if (compareMeas && frequency != 5u && frequency != 500u) continue;
                if (roktObserve && frequency != 5u && frequency != 500u) continue;
                rigol.output(1, false);
                rigol.setSine(1, frequency, 2.0, 0.0);
                rigol.output(1, true);
                if (roktObserve) {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    {
                        std::lock_guard<std::mutex> lock(roktMutex);
                        roktSamples.clear();
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(4));
                    const auto v7 = meter->query("MEAS:VOLT:AC?");
                    std::vector<RoktSample> captured;
                    {
                        std::lock_guard<std::mutex> lock(roktMutex);
                        captured = roktSamples;
                    }
                    std::cout << "POINT frequency_hz=" << frequency
                              << " rigol_vpp=2 v7_vrms=" << v7 << std::endl;
                    printRoktSamples(frequency, captured);
                    continue;
                }
                if (compareMeas) {
                    meter->write("CONF:VOLT:AC");
                    meter->write("SENS:DET:BAND 3");
                    std::this_thread::sleep_for(std::chrono::seconds(12));
                    const auto beforeBand = meter->query("SENS:DET:BAND?");
                    const auto beforeRms = meter->query("READ?");
                    const auto measuredRms = meter->query("MEAS:VOLT:AC?");
                    const auto afterMeasBand = meter->query("SENS:DET:BAND?");
                    meter->write("CONF:VOLT:AC");
                    meter->write("SENS:DET:BAND 3");
                    std::this_thread::sleep_for(std::chrono::seconds(12));
                    const auto afterRms = meter->query("READ?");
                    const auto restoredBand = meter->query("SENS:DET:BAND?");
                    std::cout << "COMPARE frequency_hz=" << frequency
                              << " rigol_vpp=2 band_before=" << beforeBand
                              << " read_before_vrms=" << beforeRms
                              << " meas_vrms=" << measuredRms
                              << " band_after_meas=" << afterMeasBand
                              << " read_after_vrms=" << afterRms
                              << " restored_band=" << restoredBand << std::endl;
                    continue;
                }
                for (const unsigned band : {20u, 3u}) {
                    meter->write("SENS:DET:BAND " + std::to_string(band));
                    const auto bandBefore = meter->query("SENS:DET:BAND?");
                    std::this_thread::sleep_for(std::chrono::seconds(12));
                    const auto rms = meter->query("READ?");
                    const auto bandAfter = meter->query("SENS:DET:BAND?");
                    std::cout << "POINT frequency_hz=" << frequency
                              << " rigol_vpp=2 band_requested_hz=" << band
                              << " band_before=" << bandBefore
                              << " v7_vrms=" << rms
                              << " band_after=" << bandAfter << std::endl;
                }
            }
            cleanup();
            return cleanupOk ? 0 : 4;
        } catch (...) {
            cleanup();
            throw;
        }
    } catch (const std::exception& error) {
        std::cerr << "FATAL " << error.what() << std::endl;
        return 3;
    }
}
