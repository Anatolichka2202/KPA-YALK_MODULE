#include "hardware/stand_config.h"
#include "hardware/akip1160.h"
#include "hardware/isd_router.h"
#include "hardware/bench_instruments.h"
#include "hardware/visa_instrument.h"
#include "hardware/yalk_reference_link.h"

#include <QCoreApplication>

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 3) {
        std::cerr << "Usage: tu_point_probe <stand.yaml> state|v7_band|power_on|power_off|sensor89|sensor_all|yalk1|yalk1_all\n";
        return 2;
    }
    try {
        const auto config = tu::hardware::loadStandConfig(argv[1]);
        tu::hardware::Akip1160 supply(config.supply);
        const auto power = supply.readState();
        std::cout << "AKIP output=" << power.outputEnabled
                  << " set_v=" << power.voltageSetpointV
                  << " measured_v=" << power.measuredVoltageV
                  << " measured_a=" << power.measuredCurrentA << std::endl;
        const std::string mode = argv[2];
        if (mode == "state") return 0;
        if (mode == "v7_band") {
            tu::hardware::VisaInstrument meter({config.v7.resourceExpressions,
                                                config.v7.timeoutMilliseconds});
            std::cout << "V7 resource=" << meter.resourceName()
                      << " detector_band_hz="
                      << meter.query("SENSe:DETector:BANDwidth?") << std::endl;
            return 0;
        }
        if (mode == "power_on") {
            if (power.outputEnabled) throw std::runtime_error("Power already on; not cycling it");
            try {
                supply.setCurrentLimit(0.6);
                supply.setVoltage(27.0);
                supply.setOutput(true);
                std::this_thread::sleep_for(std::chrono::seconds(30));
                const auto ready = supply.readState();
                std::cout << "AKIP_AFTER_ON output=" << ready.outputEnabled
                          << " measured_v=" << ready.measuredVoltageV
                          << " measured_a=" << ready.measuredCurrentA << std::endl;
                if (!ready.outputEnabled || ready.measuredVoltageV < 26.5
                    || ready.measuredVoltageV > 27.5)
                    throw std::runtime_error("27 V output not confirmed");
                return 0;
            } catch (...) {
                supply.safeOff();
                throw;
            }
        }
        if (mode == "power_off") {
            supply.safeOff();
            const auto off = supply.readState();
            std::cout << "AKIP_AFTER_OFF output=" << off.outputEnabled
                      << " measured_v=" << off.measuredVoltageV << std::endl;
            return off.outputEnabled ? 1 : 0;
        }
        if (!power.outputEnabled || power.measuredVoltageV < 24.0
            || power.measuredVoltageV > 35.0)
            throw std::runtime_error("Probe refused: AKIP is not already powering UBSI at 24..35 V");

        tu::hardware::IsdRouter isd(config.isd);
        isd.setTraceSink([](const tu::hardware::IsdRequestTrace& t) {
            std::cout << "ISD seq=" << t.sequence << " path=" << t.path
                      << " http=" << t.httpStatus << " latency_ms="
                      << t.latencyMilliseconds << " timeout=" << t.timeout
                      << " ack=" << t.accepted << " response=" << t.response
                      << std::endl;
        });
        if (mode == "sensor89" || mode == "sensor_all") {
            const unsigned last = mode == "sensor89" ? 89u : 94u;
            tu::hardware::V7Meter meter(config.v7);
            bool allOk = true;
            for (unsigned channel = 89; channel <= last; ++channel) {
            bool routed = false;
            try {
                // Only owner-confirmed X1..X3 pin 36/37 routes are touched.
                isd.setAnalog(channel, 819, false);
                isd.setSwitch(3, channel, false);
                routed = true;
                isd.setSwitch(3, channel, true);
                std::this_thread::sleep_for(std::chrono::milliseconds(350));
                const double v7 = meter.readDcVoltage();
                const unsigned connector = 1u + (channel - 89u) / 2u;
                const unsigned pin = 36u + (channel - 89u) % 2u;
                std::cout << "SENSOR X" << connector << '/' << pin
                          << " CH" << channel << " V7_DC=" << v7
                          << " criterion=6.0..6.4V verdict="
                          << (v7 >= 6.0 && v7 <= 6.4 ? "OK" : "FAIL")
                          << std::endl;
                allOk = allOk && v7 >= 6.0 && v7 <= 6.4;
                isd.setSwitch(3, channel, false);
                routed = false;
                std::cout << "CLEANUP type3 CH" << channel << " OFF ACK" << std::endl;
            } catch (...) {
                if (routed) {
                    try { isd.setSwitch(3, channel, false); std::cout << "CLEANUP type3 CH" << channel << " OFF ACK\n"; }
                    catch (const std::exception& e) { std::cerr << "CLEANUP FAILED: " << e.what() << '\n'; }
                }
                throw;
            }
            }
            return allOk ? 0 : 1;
        }
        if (mode == "yalk1" || mode == "yalk1_all") {
            bool driven = false;
            try {
                isd.disableYalkOutput(1);
                tu::hardware::YalkReferenceLink yalk(config.yalk);
                if (!yalk.startYalk(std::chrono::milliseconds(500),
                                    std::chrono::milliseconds(3000), {}))
                    throw std::runtime_error("Fresh reference204 did not appear");
                tu::hardware::V7Meter meter(config.v7);
                if (mode == "yalk1_all") {
                    const auto open = yalk.readYalkSnapshot(1,
                        std::chrono::milliseconds(3000), {});
                    std::cout << "YALK CH1 work=0 contact=" << open.at(0).contact
                              << " raw=" << open.at(0).rawMean << std::endl;
                }
                const double points[] = {0.0, 3.1, 4.0, 6.2};
                for (double volts : points) {
                    if (mode == "yalk1" && volts != 4.0) continue;
                    driven = true;
                    isd.setYalkVoltage(1, volts);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    const double v7 = meter.readDcVoltage();
                    const auto frame = yalk.readYalkSnapshot(1,
                        std::chrono::milliseconds(3000), {});
                    const auto& channel = frame.at(0);
                    std::cout << "YALK CH1 target=" << volts << "V V7_DC=" << v7
                              << " raw=" << channel.rawMean
                              << " code=" << channel.codeMean
                              << " contact=" << channel.contact
                              << " cal97=" << frame.at(96).codeMean
                              << " cal99=" << frame.at(98).codeMean
                              << std::endl;
                }
                isd.disableYalkOutput(1);
                driven = false;
                std::cout << "CLEANUP type1 CH1 OFF ACK" << std::endl;
                return 0;
            } catch (...) {
                if (driven) {
                    try { isd.disableYalkOutput(1); std::cout << "CLEANUP type1 CH1 OFF ACK\n"; }
                    catch (const std::exception& e) { std::cerr << "CLEANUP FAILED: " << e.what() << '\n'; }
                }
                throw;
            }
        }
        throw std::invalid_argument("Unknown probe mode");
    } catch (const std::exception& error) {
        std::cerr << "PROBE ERROR: " << error.what() << std::endl;
        return 3;
    }
}
