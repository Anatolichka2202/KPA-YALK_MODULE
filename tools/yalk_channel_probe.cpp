#include "hardware/stand_config.h"
#include "hardware/stand_hardware.h"

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

struct CommandPoint {
    double volts = 0.0;
    unsigned settleMs = 500;
};

double parseDouble(const std::string& text, const char* name)
{
    std::size_t parsed = 0;
    const double value = std::stod(text, &parsed);
    if (parsed != text.size() || !std::isfinite(value))
        throw std::invalid_argument(std::string(name) + " must be a number");
    return value;
}

unsigned parseUnsigned(const std::string& text, const char* name)
{
    std::size_t parsed = 0;
    const auto value = std::stoul(text, &parsed);
    if (parsed != text.size() || value > std::numeric_limits<unsigned>::max())
        throw std::invalid_argument(std::string(name) + " must be an unsigned integer");
    return static_cast<unsigned>(value);
}

std::vector<CommandPoint> parsePoints(const std::string& text)
{
    std::vector<CommandPoint> points;
    std::stringstream input(text);
    std::string token;
    while (std::getline(input, token, ',')) {
        if (token.empty()) throw std::invalid_argument("volts must not contain empty points");
        CommandPoint point;
        const auto separator = token.find('@');
        const auto voltsText = token.substr(0, separator);
        point.volts = parseDouble(voltsText, "volts");
        if (separator != std::string::npos) {
            const auto settleText = token.substr(separator + 1);
            if (settleText.empty()) throw std::invalid_argument("settle_ms must not be empty");
            point.settleMs = parseUnsigned(settleText, "settle_ms");
        }
        if (point.volts < 0.0 || point.volts > 6.2)
            throw std::invalid_argument("each volts point must be 0..6.2");
        points.push_back(point);
    }
    if (points.empty()) throw std::invalid_argument("at least one volts point is required");
    return points;
}

std::vector<unsigned> parseDelays(const std::string& text)
{
    std::vector<unsigned> delays;
    std::stringstream input(text);
    std::string token;
    while (std::getline(input, token, ',')) {
        if (token.empty()) throw std::invalid_argument("off_delays_ms must not contain empty values");
        delays.push_back(parseUnsigned(token, "off_delays_ms"));
    }
    if (delays.empty()) throw std::invalid_argument("off_delays_ms must not be empty");
    return delays;
}

bool isDelayList(const std::string& text)
{
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char value) {
        return std::isdigit(value) || value == ',';
    });
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
    if (argc < 2 || argc > 6) {
        std::cerr << "Usage: yalk_channel_probe <stand.yaml> [channel] "
                     "[volts[@settle_ms],...] [off_delays_ms] [log-file]\n";
        return 2;
    }
    const unsigned channel = argc >= 3 ? static_cast<unsigned>(std::stoul(argv[2])) : 87u;
    if (!channel || channel > 96) throw std::invalid_argument("channel must be 1..96");
    const auto commands = parsePoints(argc >= 4 ? argv[3] : "3.1");
    const bool fourthIsDelays = argc >= 5 && isDelayList(argv[4]);
    const auto offDelays = parseDelays(fourthIsDelays ? argv[4] : "300");
    const char* logPath = argc == 6 ? argv[5] : (argc == 5 && !fourthIsDelays ? argv[4] : nullptr);
    std::ofstream log;
    if (logPath) log.open(logPath, std::ios::out | std::ios::trunc);
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

        for (const auto& command : commands) {
            const auto switchStarted = std::chrono::steady_clock::now();
            stand.isd().setYalkVoltage(channel, command.volts);
            std::this_thread::sleep_for(std::chrono::milliseconds(command.settleMs));
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
                + " command_v=" + std::to_string(command.volts)
                + " settle_ms=" + std::to_string(command.settleMs)
                + " elapsed_ms=" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - switchStarted).count())
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
        const auto offStarted = std::chrono::steady_clock::now();
        stand.isd().disableYalkOutput(channel);
        for (const unsigned delay : offDelays) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            const double v7 = stand.v7().readDcVoltage();
            const auto frame = stand.yalk().readYalkSnapshot(
                1, std::chrono::milliseconds(3000), {});
            const auto& value = frame.at(channel - 1);
            const unsigned rawWord = static_cast<unsigned>(value.rawMean);
            write("timestamp_utc=" + timestampUtc()
                + " channel=" + std::to_string(channel)
                + " action=OFF"
                + " requested_delay_ms=" + std::to_string(delay)
                + " elapsed_since_off_ms=" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - offStarted).count())
                + " v7_v=" + std::to_string(v7)
                + " raw_word=" + std::to_string(rawWord)
                + " code=" + std::to_string(rawWord & 0x03ffu)
                + " contact=" + std::to_string((rawWord & 0x0400u) != 0 ? 1 : 0)
                + " freshness=post_off_reference204");
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
