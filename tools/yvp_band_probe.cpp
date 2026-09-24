#include "hardware/akip1160.h"
#include "hardware/bench_instruments.h"
#include "hardware/isd_router.h"
#include "hardware/stand_config.h"
#include "hardware/visa_instrument.h"

#include <QCoreApplication>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2 && !(argc == 3 && std::string(argv[2]) == "--compare-meas")) {
        std::cerr << "Usage: yvp_band_probe <stand.yaml> [--compare-meas]\n";
        return 2;
    }
    const bool compareMeas = argc == 3;

    bool ownPower = false;
    bool inputMayBeOn = false;
    bool measureMayBeOn = false;
    bool gainMayBeOn = false;
    try {
        const auto config = tu::hardware::loadStandConfig(argv[1]);
        tu::hardware::Akip1160 supply(config.supply);
        tu::hardware::RigolGenerator rigol(config.generator);
        tu::hardware::IsdRouter isd(config.isd);
        tu::hardware::VisaInstrument meter({config.v7.resourceExpressions, 25000});

        const auto cleanup = [&] {
            rigol.safeOff();
            if (gainMayBeOn) {
                try { isd.setSwitch(2, 2, false); std::cout << "CLEANUP KU2 OFF OK\n"; }
                catch (const std::exception& e) { std::cerr << "CLEANUP KU2 OFF ERROR " << e.what() << '\n'; }
            }
            if (measureMayBeOn) {
                try { isd.setSwitch(3, 44, false); std::cout << "CLEANUP V7 ROUTE OFF OK\n"; }
                catch (const std::exception& e) { std::cerr << "CLEANUP V7 ROUTE OFF ERROR " << e.what() << '\n'; }
            }
            if (inputMayBeOn) {
                try { isd.setSwitch(2, 33, false); std::cout << "CLEANUP INPUT OFF OK\n"; }
                catch (const std::exception& e) { std::cerr << "CLEANUP INPUT OFF ERROR " << e.what() << '\n'; }
            }
            if (ownPower) {
                supply.safeOff();
                const auto off = supply.readState();
                std::cout << "CLEANUP AKIP output=" << off.outputEnabled
                          << " voltage=" << off.measuredVoltageV << '\n';
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

            rigol.safeOff();
            isd.setAnalog(44, 0, false);
            isd.setSwitch(2, 33, false);
            isd.setSwitch(3, 44, false);
            isd.setSwitch(2, 2, false);
            inputMayBeOn = true;
            isd.setSwitch(2, 33, true);
            measureMayBeOn = true;
            isd.setSwitch(3, 44, true);
            gainMayBeOn = true;
            isd.setSwitch(2, 2, true);
            std::cout << "ROUTE input=type2/33 measurement=type3/44 KU2=type2/2 ACK\n";

            meter.write("CONF:VOLT:AC");
            std::cout << "V7=" << meter.resourceName() << '\n';
            for (const unsigned frequency : {5u, 10u, 20u, 500u}) {
                if (compareMeas && frequency != 5u && frequency != 500u) continue;
                rigol.output(1, false);
                rigol.setSine(1, frequency, 2.0, 0.0);
                rigol.output(1, true);
                if (compareMeas) {
                    meter.write("CONF:VOLT:AC");
                    meter.write("SENS:DET:BAND 3");
                    std::this_thread::sleep_for(std::chrono::seconds(12));
                    const auto beforeBand = meter.query("SENS:DET:BAND?");
                    const auto beforeRms = meter.query("READ?");
                    const auto measuredRms = meter.query("MEAS:VOLT:AC?");
                    const auto afterMeasBand = meter.query("SENS:DET:BAND?");
                    meter.write("CONF:VOLT:AC");
                    meter.write("SENS:DET:BAND 3");
                    std::this_thread::sleep_for(std::chrono::seconds(12));
                    const auto afterRms = meter.query("READ?");
                    const auto restoredBand = meter.query("SENS:DET:BAND?");
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
                    meter.write("SENS:DET:BAND " + std::to_string(band));
                    const auto bandBefore = meter.query("SENS:DET:BAND?");
                    std::this_thread::sleep_for(std::chrono::seconds(12));
                    const auto rms = meter.query("READ?");
                    const auto bandAfter = meter.query("SENS:DET:BAND?");
                    std::cout << "POINT frequency_hz=" << frequency
                              << " rigol_vpp=2 band_requested_hz=" << band
                              << " band_before=" << bandBefore
                              << " v7_vrms=" << rms
                              << " band_after=" << bandAfter << std::endl;
                }
            }
            cleanup();
            return 0;
        } catch (...) {
            cleanup();
            throw;
        }
    } catch (const std::exception& error) {
        std::cerr << "FATAL " << error.what() << std::endl;
        return 3;
    }
}
