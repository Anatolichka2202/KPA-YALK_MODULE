#include "hardware/stand_config.h"
#include "hardware/stand_hardware.h"

#include <QCoreApplication>

#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

std::vector<double> parsePoints(const std::string& text)
{
    std::vector<double> points;
    std::stringstream input(text);
    std::string token;
    while (std::getline(input, token, ',')) {
        if (token.empty()) throw std::invalid_argument("volts must not contain empty points");
        const double point = std::stod(token);
        if (point < 0.0 || point > 6.2)
            throw std::invalid_argument("each volts point must be 0..6.2");
        points.push_back(point);
    }
    if (points.empty()) throw std::invalid_argument("at least one volts point is required");
    return points;
}

std::string timestampUtc()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::ostringstream result;
    result << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return result.str();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc < 2 || argc > 5) {
        std::cerr << "Usage: yalk_channel_probe <stand.yaml> [channel] [volts[,volts...]] [log-file]\n";
        return 2;
    }
    const unsigned channel = argc >= 3 ? static_cast<unsigned>(std::stoul(argv[2])) : 87u;
    if (!channel || channel > 96) throw std::invalid_argument("channel must be 1..96");
    const auto commands = parsePoints(argc >= 4 ? argv[3] : "3.1");
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
        // The measured output is still transient immediately after OUTP ON.
        // Wait before treating a readback as a failed hardware condition.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
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

        for (const double command : commands) {
            stand.isd().setYalkVoltage(channel, command);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const double v7 = stand.v7().readDcVoltage();
            // One post-switch frame preserves the unaveraged 16-bit word.
            const auto frame = stand.yalk().readYalkSnapshot(
                1, std::chrono::milliseconds(3000), {});
            stand.isd().disableYalkOutput(channel);

            const double zero = frame.at(96).codeMean;
            const double full = frame.at(98).codeMean;
            const auto& value = frame.at(channel - 1);
            const unsigned rawWord = static_cast<unsigned>(value.rawMean);
            const unsigned code = rawWord & 0x03ffu;
            const bool contact = (rawWord & 0x0400u) != 0;
            const double volts = (code - zero) * 6.2 / (full - zero);
            std::ostringstream rawHex;
            rawHex << "0x" << std::uppercase << std::hex << std::setw(4)
                   << std::setfill('0') << rawWord;
            write("timestamp_utc=" + timestampUtc()
                + " channel=" + std::to_string(channel)
                + " command_v=" + std::to_string(command)
                + " v7_v=" + std::to_string(v7)
                + " raw_word=" + std::to_string(rawWord)
                + " raw_hex=" + rawHex.str()
                + " code=" + std::to_string(code)
                + " contact=" + std::to_string(contact ? 1 : 0)
                + " code97=" + std::to_string(zero)
                + " code99=" + std::to_string(full)
                + " yalk_v=" + std::to_string(volts)
                + " error_v=" + std::to_string(std::abs(volts - v7))
                + " freshness=post_switch_reference204");
        }
        stand.yalk().stop();
        stand.supply().safeOff();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const auto cleanup = stand.supply().readState();
        write("cleanup_power_output=" + std::to_string(cleanup.outputEnabled ? 1 : 0)
            + " cleanup_power_measured_v=" + std::to_string(cleanup.measuredVoltageV)
            + " cleanup_power_current_a=" + std::to_string(cleanup.measuredCurrentA));
        if (cleanup.outputEnabled)
            throw std::runtime_error("АКИП не подтвердил отключение после пробы ЯЛК");
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
