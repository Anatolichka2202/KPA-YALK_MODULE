#include "hardware/stand_config.h"
#include "hardware/stand_hardware.h"

#include <QCoreApplication>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc < 2 || argc > 5) {
        std::cerr << "Usage: yalk_channel_probe <stand.yaml> [channel] [volts] [log-file]\n";
        return 2;
    }
    const unsigned channel = argc >= 3 ? static_cast<unsigned>(std::stoul(argv[2])) : 87u;
    if (!channel || channel > 96) throw std::invalid_argument("channel must be 1..96");
    const double command = argc >= 4 ? std::stod(argv[3]) : 3.1;
    if (command < 0.0 || command > 6.2)
        throw std::invalid_argument("volts must be 0..6.2");
    std::ofstream log;
    if (argc == 5) log.open(argv[4], std::ios::out | std::ios::trunc);
    const auto write = [&log](const std::string& line) {
        std::cout << line << std::endl;
        if (log) { log << line << '\n'; log.flush(); }
    };

    tu::hardware::StandHardware stand(tu::hardware::loadStandConfig(argv[1]));
    try {
        stand.supply().setCurrentLimit(0.6);
        stand.supply().setVoltage(27.0);
        stand.supply().setOutput(true);
        const auto power = stand.supply().readState();
        write("power_output=" + std::to_string(power.outputEnabled ? 1 : 0)
            + " power_setpoint_v=" + std::to_string(power.voltageSetpointV)
            + " power_measured_v=" + std::to_string(power.measuredVoltageV)
            + " power_current_a=" + std::to_string(power.measuredCurrentA));
        if (!power.outputEnabled || power.measuredVoltageV < 26.5 || power.measuredVoltageV > 27.5)
            throw std::runtime_error("АКИП не подтвердил рабочее питание 27 В перед пробой ЯЛК");
        // Адаптер питается от того же АКИП. Для диагностической пробы даём
        // полный нормативный интервал готовности 30 с; повторных циклов
        // питания внутри пробы нет.
        std::this_thread::sleep_for(std::chrono::seconds(30));

        // Global type=4 is intentionally not used: the replaced KM currently
        // makes KMGlobalBusReset fail.  Only the tested channel is touched.
        stand.isd().disableYalkOutput(channel);
        const bool ready = stand.yalk().startYalk(
            std::chrono::milliseconds(500), std::chrono::milliseconds(3000), {});
        if (!ready) throw std::runtime_error("reference204 not received");

        stand.isd().setYalkVoltage(channel, command);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const double v7 = stand.v7().readDcVoltage();
        const auto frame = stand.yalk().readYalkSnapshot(
            16, std::chrono::milliseconds(3000), {});
        stand.isd().disableYalkOutput(channel);

        const double zero = frame.at(96).codeMean;
        const double full = frame.at(98).codeMean;
        const auto& value = frame.at(channel - 1);
        const double volts = (value.codeMean - zero) * 6.2 / (full - zero);
        write("channel=" + std::to_string(channel)
            + " command_v=" + std::to_string(command)
            + " raw=" + std::to_string(value.rawMean)
            + " code=" + std::to_string(value.codeMean)
            + " signal=" + std::to_string(value.contact ? 1 : 0)
            + " code97=" + std::to_string(zero)
            + " code99=" + std::to_string(full)
            + " yalk_v=" + std::to_string(volts)
            + " v7_v=" + std::to_string(v7)
            + " error_v=" + std::to_string(std::abs(volts - v7)));
        stand.yalk().stop();
        stand.supply().safeOff();
        return 0;
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
