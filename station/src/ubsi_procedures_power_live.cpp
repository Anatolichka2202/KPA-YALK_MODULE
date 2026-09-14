#include "orbita_stand/ubsi_procedures.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace orbita::stand {
namespace {

std::map<std::string, std::string> responseValues(const std::string& response)
{
    std::map<std::string, std::string> result;
    std::stringstream stream(response);
    std::string line;
    while (std::getline(stream, line)) {
        const auto equal = line.find('=');
        if (equal != std::string::npos && equal != 0)
            result[line.substr(0, equal)] = line.substr(equal + 1);
    }
    return result;
}

std::string argument(const ScenarioNode& node, const std::string& key,
                     std::string fallback = {})
{
    const auto found = node.arguments.find(key);
    return found == node.arguments.end() ? std::move(fallback) : found->second;
}

double number(const ScenarioNode& node, const std::string& key, double fallback = 0.0)
{
    const auto text = argument(node, key);
    if (text.empty()) return fallback;
    std::size_t parsed = 0;
    const double value = std::stod(text, &parsed);
    if (parsed != text.size() || !std::isfinite(value))
        throw std::invalid_argument("Некорректный аргумент " + key);
    return value;
}

unsigned natural(const ScenarioNode& node, const std::string& key, unsigned fallback = 0)
{
    const auto text = argument(node, key);
    if (text.empty()) return fallback;
    std::size_t parsed = 0;
    const auto value = std::stoul(text, &parsed, 0);
    if (parsed != text.size()) throw std::invalid_argument("Некорректный аргумент " + key);
    return static_cast<unsigned>(value);
}

std::vector<double> numbers(const ScenarioNode& node, const std::string& key)
{
    std::vector<double> result;
    std::stringstream stream(argument(node, key));
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) result.push_back(std::stod(item));
    }
    return result;
}

double responseNumber(const std::string& response, const std::string& key)
{
    const auto values = responseValues(response);
    const auto found = values.find(key);
    if (found == values.end()) throw std::runtime_error("Оборудование не вернуло поле " + key);
    return std::stod(found->second);
}

unsigned responseUnsigned(const std::string& response, const std::string& key)
{
    const auto values = responseValues(response);
    const auto found = values.find(key);
    if (found == values.end()) throw std::runtime_error("Оборудование не вернуло поле " + key);
    return static_cast<unsigned>(std::stoul(found->second));
}

RunVerdict limit(double value, double lower, double upper)
{
    return std::isfinite(value) && value >= lower && value <= upper
        ? RunVerdict::Ok : RunVerdict::Fail;
}

MeasurementResult measurement(std::string key, std::string title,
                              double reference, double measured,
                              double lower, double upper, std::string unit)
{
    MeasurementResult result;
    result.parameterKey = std::move(key);
    result.title = std::move(title);
    result.reference = reference;
    result.measured = measured;
    result.lowerLimit = lower;
    result.upperLimit = upper;
    result.unit = std::move(unit);
    result.verdict = limit(measured, lower, upper);
    if (result.verdict == RunVerdict::Fail) result.message = "Значение вне допуска";
    return result;
}

void append(ProcedureResult& result, MeasurementResult value)
{
    result.verdict = combineVerdicts(result.verdict, value.verdict);
    result.measurements.push_back(std::move(value));
}

void wait(ProcedureContext& context, unsigned milliseconds)
{
    double scale = 1.0;
    if (const char* text = std::getenv("MILTECH_TIME_SCALE")) {
        try { scale = std::clamp(std::stod(text), 0.001, 1.0); } catch (...) {}
    }
    milliseconds = static_cast<unsigned>(std::max(1.0, milliseconds * scale));
    constexpr unsigned slice = 50;
    for (unsigned elapsed = 0; elapsed < milliseconds; elapsed += slice) {
        if (context.stopRequested.load()) throw std::runtime_error("Остановлено оператором");
        std::this_thread::sleep_for(std::chrono::milliseconds(
            std::min(slice, milliseconds - elapsed)));
    }
}

bool restartYalk(ProcedureContext& context, unsigned timeoutMs)
{
    const auto started = std::chrono::steady_clock::now();
    do {
        if (context.stopRequested.load()) throw std::runtime_error("Остановлено оператором");
        try {
            context.equipment.invoke("ulk.parameter_source", "prepare_yalk_reference", {});
            const auto response = responseValues(context.equipment.invoke(
                "ulk.parameter_source", "start_prepared_yalk_reference", {
                    {"timeout_ms", std::to_string(std::min(timeoutMs, 1000u))}}));
            if (response.count("status") && response.at("status") == "ready") return true;
        } catch (const std::exception&) {
        }
        const auto elapsed = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
        if (elapsed >= timeoutMs) return false;
        wait(context, std::min(250u, timeoutMs - elapsed));
    } while (true);
}

std::vector<unsigned> parseUnsignedCsv(const std::string& text)
{
    std::vector<unsigned> result;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (!token.empty()) result.push_back(static_cast<unsigned>(std::stoul(token)));
    }
    return result;
}

std::vector<unsigned> powerYalkAddresses(ProcedureContext& context)
{
    constexpr const char* cacheKey = "power_yalk.addresses";
    const auto cached = context.state.find(cacheKey);
    if (cached != context.state.end()) return parseUnsignedCsv(cached->second);
    if (!context.equipment.hasCapability("catalog.parameter_resolver")) return {};

    std::vector<unsigned> addresses;
    addresses.reserve(80);
    std::ostringstream cache;
    for (unsigned channel = 0; channel < 80; ++channel) {
        const auto values = responseValues(context.equipment.invoke(
            "catalog.parameter_resolver", "resolve", {
                {"block_type", "UBSI_468157_002"},
                {"parameter_group", "yalk_voltage"},
                {"channel_index", std::to_string(channel)}}));
        const auto source = values.find("source");
        const auto locator = values.find("locator");
        const auto confirmed = values.find("confirmed");
        if (source == values.end() || source->second != "ulk.parameter_source"
            || locator == values.end() || locator->second.empty()
            || confirmed == values.end() || confirmed->second != "true") {
            return {};
        }
        const unsigned address = static_cast<unsigned>(std::stoul(locator->second));
        if (address == 0 || address > 100) return {};
        addresses.push_back(address);
        if (channel) cache << ',';
        cache << address;
    }
    context.state[cacheKey] = cache.str();
    return addresses;
}

void publishPowerYalk(ProcedureContext& context, const ScenarioNode& node, double setpoint)
{
    std::map<std::string, std::string> data{
        {"setpoint_v", std::to_string(setpoint)},
        {"fresh", "false"},
        {"channel_count", "80"}};
    try {
        const auto addresses = powerYalkAddresses(context);
        if (addresses.size() != 80) {
            data["detail"] = "Нет подтверждённой карты 80 каналов ЯЛК";
            context.eventSink({std::chrono::system_clock::now(), node.id, "POWER_YALK",
                "ЯЛК во время проверки питания", RunVerdict::NotRun, data});
            return;
        }

        const unsigned after = responseUnsigned(
            context.equipment.invoke("ulk.parameter_source", "stats", {}), "last_sequence");
        const auto response = responseValues(context.equipment.invoke(
            "ulk.parameter_source", "read_snapshot", {
                {"after_sequence", std::to_string(after)},
                {"timeout_ms", "250"}}));
        const auto wordsIt = response.find("words");
        if (wordsIt == response.end()) throw std::runtime_error("Адаптер не вернул words");
        const auto words = parseUnsignedCsv(wordsIt->second);
        if (words.size() < 100) throw std::runtime_error("В снимке ЯЛК меньше 100 слов");

        const double zero = static_cast<double>(words[96] & 0x03FFu);  // address 97
        const double full = static_cast<double>(words[98] & 0x03FFu);  // address 99
        if (!(full > zero)) throw std::runtime_error("Недостоверная шкала 97/99");

        std::ostringstream volts;
        std::ostringstream codes;
        volts.precision(10);
        for (std::size_t index = 0; index < addresses.size(); ++index) {
            const double code = static_cast<double>(words[addresses[index] - 1] & 0x03FFu);
            const double value = (code - zero) * 6.2 / (full - zero);
            if (index) { volts << ','; codes << ','; }
            volts << value;
            codes << code;
        }
        data["fresh"] = "true";
        data["values_v"] = volts.str();
        data["values_code"] = codes.str();
        data["zero_code"] = std::to_string(zero);
        data["full_code"] = std::to_string(full);
        const auto sequence = response.find("sequence");
        if (sequence != response.end()) data["sequence"] = sequence->second;
    } catch (const std::exception& error) {
        data["detail"] = error.what();
    }

    context.eventSink({std::chrono::system_clock::now(), node.id, "POWER_YALK",
        "ЯЛК во время проверки питания", RunVerdict::NotRun, data});
}

ProcedureResult supplyRangeLiveYalk(const ScenarioNode& node, ProcedureContext& context)
{
    const auto points = numbers(node, "voltage_points_v");
    if (points.empty()) throw std::invalid_argument("Для проверки питания нужны voltage_points_v");
    const auto survival = numbers(node, "survival_points_v");
    const auto durations = numbers(node, "survival_seconds");
    if (survival.size() != durations.size())
        throw std::invalid_argument("survival_points_v и survival_seconds должны совпадать");

    const double hardwareCurrentLimit = number(node, "supply_current_limit_a", 0.6);
    const double totalCurrentLimit = number(node, "maximum_total_current_a",
        number(node, "current_limit_a", 0.4));
    const double voltageTolerance = number(node, "voltage_tolerance_v", 0.5);
    const double restoreVoltage = number(node, "restore_voltage_v", 27.0);
    const unsigned settleMs = natural(node, "settle_ms", 500);
    const unsigned recoveryTimeoutMs = natural(node, "recovery_timeout_ms", 30000);

    ProcedureResult result{RunVerdict::Ok,
        "Проверены рабочий диапазон питания и общий ток потребления УБСИ", {}};
    const auto publishSupply = [&](double setpoint, const std::string& state,
                                   unsigned elapsed = 0, unsigned duration = 0) {
        context.eventSink({std::chrono::system_clock::now(), node.id, "SUPPLY", "Общий ток УБСИ",
            RunVerdict::NotRun,
            {{"setpoint_v", std::to_string(setpoint)},
             {"volts", std::to_string(responseNumber(state, "volts"))},
             {"amperes", std::to_string(responseNumber(state, "amperes"))},
             {"elapsed_s", std::to_string(elapsed)},
             {"duration_s", std::to_string(duration)}}});
    };
    const auto restore = [&] {
        context.equipment.invoke("power.dc_supply", "set_voltage", {
            {"volts", std::to_string(restoreVoltage)}});
        wait(context, settleMs);
    };

    try {
        context.equipment.invoke("power.dc_supply", "set_current_limit", {
            {"amperes", std::to_string(hardwareCurrentLimit)}});
        context.equipment.invoke("power.dc_supply", "output", {{"enabled", "true"}});

        for (const double setpoint : points) {
            context.equipment.invoke("power.dc_supply", "set_voltage", {
                {"volts", std::to_string(setpoint)}});
            wait(context, settleMs);
            const auto state = context.equipment.invoke("power.dc_supply", "read_state", {});
            publishSupply(setpoint, state);
            publishPowerYalk(context, node, setpoint);
            const double actualVoltage = responseNumber(state, "volts");
            const double totalCurrent = responseNumber(state, "amperes");

            auto voltage = measurement("ubsi.supply.voltage",
                "Фактическое питание при уставке " + std::to_string(setpoint) + " В",
                setpoint, actualVoltage, setpoint - voltageTolerance,
                setpoint + voltageTolerance, "В");
            voltage.attributes = {{"setpoint_v", std::to_string(setpoint)},
                                  {"supply_current_a", std::to_string(totalCurrent)}};
            append(result, std::move(voltage));

            auto current = measurement("ubsi.supply.total_current",
                "Общий ток потребления УБСИ при " + std::to_string(setpoint) + " В",
                0.0, totalCurrent, 0.0, totalCurrentLimit, "А");
            current.attributes = {{"setpoint_v", std::to_string(setpoint)},
                                  {"measurement_scope", "whole_ubsi"},
                                  {"hardware_current_limit_a", std::to_string(hardwareCurrentLimit)}};
            append(result, std::move(current));

            const auto alive = responseValues(context.equipment.invoke(
                "ulk.parameter_source", "alive", {{"timeout_ms", "3000"}}));
            append(result, measurement("ubsi.supply.data_alive",
                "Передача данных при " + std::to_string(setpoint) + " В", 1.0,
                alive.count("status") && alive.at("status") == "ready" ? 1.0 : 0.0,
                1.0, 1.0, "лог."));
        }

        for (std::size_t index = 0; index < survival.size(); ++index) {
            context.equipment.invoke("power.dc_supply", "set_voltage", {
                {"volts", std::to_string(survival[index])}});
            const unsigned durationMs = static_cast<unsigned>(durations[index] * 1000.0);
            for (unsigned elapsed = 0; elapsed < durationMs;) {
                const auto state = context.equipment.invoke("power.dc_supply", "read_state", {});
                publishSupply(survival[index], state, elapsed / 1000, durationMs / 1000);
                publishPowerYalk(context, node, survival[index]);
                const unsigned interval = std::min(1000u, durationMs - elapsed);
                wait(context, interval);
                elapsed += interval;
            }
            const auto state = context.equipment.invoke("power.dc_supply", "read_state", {});
            publishSupply(survival[index], state, durationMs / 1000, durationMs / 1000);
            publishPowerYalk(context, node, survival[index]);
            const double actualVoltage = responseNumber(state, "volts");
            auto held = measurement("ubsi.supply.survival_voltage",
                "Фактическое напряжение выдержки " + std::to_string(survival[index]) + " В",
                survival[index], actualVoltage, survival[index] - voltageTolerance,
                survival[index] + voltageTolerance, "В");
            held.attributes = {{"duration_s", std::to_string(durations[index])}};
            append(result, std::move(held));

            restore();
            append(result, measurement("ubsi.supply.survived",
                "Работоспособность после предельного напряжения и возврата к 27 В",
                1.0, restartYalk(context, recoveryTimeoutMs) ? 1.0 : 0.0,
                1.0, 1.0, "лог."));
            publishPowerYalk(context, node, restoreVoltage);
        }
        restore();
        publishPowerYalk(context, node, restoreVoltage);
    } catch (...) {
        try { restore(); } catch (...) {}
        throw;
    }
    return result;
}

} // namespace

void registerPowerLiveUbsiProcedures(ScenarioEngine& engine)
{
    // Replace only the supply-range implementation. The method and normative
    // measurements remain the same; POWER_YALK events are UI evidence and do
    // not alter the product verdict.
    engine.registerProcedure("ubsi.supply_range", supplyRangeLiveYalk);
}

} // namespace orbita::stand
