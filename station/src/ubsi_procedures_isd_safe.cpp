#include "orbita_stand/ubsi_procedures.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

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

unsigned natural(const ScenarioNode& node, const std::string& key, unsigned fallback = 0)
{
    const auto text = argument(node, key);
    if (text.empty()) return fallback;
    std::size_t parsed = 0;
    const auto value = std::stoul(text, &parsed, 0);
    if (parsed != text.size()) throw std::invalid_argument("Некорректный аргумент " + key);
    return static_cast<unsigned>(value);
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

bool responseBool(const std::string& response, const std::string& key)
{
    const auto values = responseValues(response);
    const auto found = values.find(key);
    if (found == values.end()) throw std::runtime_error("Оборудование не вернуло поле " + key);
    return found->second == "1" || found->second == "true" || found->second == "yes";
}

void waitScaled(ProcedureContext& context, unsigned milliseconds)
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

RunVerdict limit(double value, double lower, double upper)
{
    return std::isfinite(value) && value >= lower && value <= upper
        ? RunVerdict::Ok : RunVerdict::Fail;
}

MeasurementResult measurement(
    std::string key, std::string title, double reference, double measured,
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

struct LogicalBinding {
    std::string locator;
    bool confirmed = false;
};

LogicalBinding resolveBinding(
    ProcedureContext& context, const std::string& parameterGroup, unsigned channel)
{
    const auto values = responseValues(context.equipment.invoke(
        "catalog.parameter_resolver", "resolve", {
            {"block_type", "UBSI_468157_002"},
            {"parameter_group", parameterGroup},
            {"channel_index", std::to_string(channel)}}));
    const auto locator = values.find("locator");
    const auto confirmed = values.find("confirmed");
    if (locator == values.end() || confirmed == values.end()) {
        throw std::runtime_error("Каталог не вернул locator/confirmed для " + parameterGroup);
    }
    return {locator->second, confirmed->second == "true"};
}

struct UlkChannelValue {
    double code = 0.0;
    bool signal = false;
    unsigned raw = 0;
    unsigned lastSequence = 0;
};

unsigned ulkLastSequence(ProcedureContext& context)
{
    return responseUnsigned(context.equipment.invoke(
        "ulk.parameter_source", "stats", {}), "last_sequence");
}

UlkChannelValue readUlkChannel(
    ProcedureContext& context, unsigned address, unsigned samples, unsigned afterSequence)
{
    const auto response = context.equipment.invoke("ulk.parameter_source", "read_channel", {
        {"ulk_address", std::to_string(address)},
        {"sample_count", std::to_string(samples)},
        {"after_sequence", std::to_string(afterSequence)},
        {"timeout_ms", "3000"}});
    return {
        responseNumber(response, "analog_code_mean"),
        responseBool(response, "signal"),
        static_cast<unsigned>(std::llround(responseNumber(response, "raw_mean"))),
        responseUnsigned(response, "last_sequence")};
}

double stateNumber(const ProcedureContext& context, const std::string& key)
{
    const auto found = context.state.find(key);
    if (found == context.state.end()) throw std::runtime_error("Нет состояния сценария ЯЛК: " + key);
    return std::stod(found->second);
}

double yalkCodeToVolts(double code, const ProcedureContext& context)
{
    const double zero = stateNumber(context, "yalk.zero_code");
    const double full = stateNumber(context, "yalk.full_code");
    const double voltage = stateNumber(context, "yalk.full_voltage");
    if (!(full > zero)) throw std::runtime_error("Неверная калибровка ЯЛК");
    return (code - zero) * voltage / (full - zero);
}

std::string yalkOwner(const ProcedureContext& context)
{
    return "run:" + context.runId + ":yalk";
}

ProcedureResult yalkStartStreamSafe(const ScenarioNode& node, ProcedureContext& context)
{
    // ISD preparation was previously type=4 -> 400 ms -> type=7. Firmware
    // source proves case 7 has no confirmed action and type=4 is a long global
    // all-off sweep. YALK stream startup therefore configures only the adapter.
    context.equipment.invoke("ulk.parameter_source", "start_record", {{"run_id", context.runId}});
    context.equipment.invoke("ulk.parameter_source", "prepare_yalk_reference", {});
    waitScaled(context, natural(node, "configure_settle_ms", 500));
    const auto response = responseValues(context.equipment.invoke(
        "ulk.parameter_source", "start_prepared_yalk_reference", {
            {"timeout_ms", std::to_string(natural(node, "timeout_ms", 3000))}}));
    if (!response.count("status") || response.at("status") != "ready") {
        throw std::runtime_error("Адаптер не выдал медленный кадр ЯЛК");
    }
    if (response.count("first_sequence"))
        context.state["yalk.first_sequence"] = response.at("first_sequence");
    return {RunVerdict::Ok,
        "Адаптер запущен в режиме ЯЛК без неподтверждённой команды подготовки ИСД", {}};
}

ProcedureResult yalkCheckInitialSafe(const ScenarioNode& node, ProcedureContext& context)
{
    const unsigned count = natural(node, "channel_count", 80);
    for (unsigned channel = 0; channel < count; ++channel) {
        const auto binding = resolveBinding(context, "yalk_voltage", channel);
        if (!binding.confirmed || binding.locator.empty()) {
            return {RunVerdict::Incomplete, "Адреса ЯЛК не подтверждены", {}};
        }
    }

    // Release only routes previously owned by this process. Do not establish a
    // fictitious global safe state by issuing firmware type=4.
    context.equipment.invoke("stand.switch_matrix", "release_owner", {
        {"owner", yalkOwner(context)}});

    ProcedureResult result{RunVerdict::Ok,
        "Проверено наблюдаемое отключённое состояние ЯЛК без глобального reset ИСД", {}};
    unsigned sequence = ulkLastSequence(context);
    const double fullScale = number(node, "full_scale_v", 6.2);
    for (unsigned channel = 0; channel < count; ++channel) {
        const auto binding = resolveBinding(context, "yalk_voltage", channel);
        const unsigned address = static_cast<unsigned>(std::stoul(binding.locator));
        const auto reading = readUlkChannel(
            context, address, natural(node, "sample_count", 4), sequence);
        sequence = reading.lastSequence;
        const double volts = yalkCodeToVolts(reading.code, context);
        auto analog = measurement("ubsi.yalk.initial." + binding.locator,
            "ЯЛК адрес " + binding.locator + ": обрыв, аналоговый вход",
            0.0, volts, -fullScale, 0.0, "В");
        if (!(volts < 0.0)) {
            analog.verdict = RunVerdict::Fail;
            analog.message = "При обрыве значение ЯЛК должно быть ниже 0 В";
        }
        analog.attributes = {{"ulk_address", binding.locator},
                             {"raw", std::to_string(reading.raw)},
                             {"analog_code", std::to_string(reading.code)},
                             {"yalk_v", std::to_string(volts)},
                             {"signal", reading.signal ? "1" : "0"},
                             {"isd_global_state", "not_readable"}};
        append(result, std::move(analog));
        append(result, measurement("ubsi.yalk.initial.signal." + binding.locator,
            "ЯЛК адрес " + binding.locator + ": исходный сигнал",
            1, reading.signal ? 1 : 0, 1, 1, "лог."));
    }
    return result;
}

ProcedureResult yalkSafeCleanupSafe(const ScenarioNode& node, ProcedureContext& context)
{
    std::string failures;
    try {
        context.equipment.invoke("stand.switch_matrix", "release_owner", {
            {"owner", yalkOwner(context)}});
    } catch (const std::exception& error) {
        failures = error.what();
    }

    try { context.equipment.invoke("ulk.parameter_source", "stop_stream", {}); }
    catch (const std::exception& error) {
        if (!failures.empty()) failures += "; ";
        failures += error.what();
    }
    try { context.equipment.invoke("ulk.parameter_source", "stop_record", {}); }
    catch (const std::exception& error) {
        if (!failures.empty()) failures += "; ";
        failures += error.what();
    }

    if (!failures.empty()) {
        return {RunVerdict::Error,
            "Не все targeted cleanup операции ЯЛК выполнены: " + failures, {}};
    }

    waitScaled(context, natural(node, "settle_ms", 300));
    const double residual = responseNumber(context.equipment.invoke(
        "measure.reference_voltage", "read_voltage", {}), "volts");
    const double maximum = number(node, "maximum_residual_voltage_v", 0.2);
    ProcedureResult result{RunVerdict::Ok,
        "Owned маршруты ЯЛК адресно выключены; поток адаптера остановлен", {}};
    auto value = measurement("ubsi.yalk.cleanup_voltage",
        "Остаточное напряжение после targeted cleanup ЯЛК",
        0.0, residual, -maximum, maximum, "В");
    value.attributes = {{"v7_v", std::to_string(residual)},
                        {"cleanup_voltage_v", std::to_string(residual)},
                        {"global_isd_reset", "not_used"}};
    append(result, std::move(value));
    return result;
}

} // namespace

void registerIsdSafeUbsiProcedures(ScenarioEngine& engine)
{
    engine.registerProcedure("yalk.start_stream", yalkStartStreamSafe);
    engine.registerProcedure("yalk.check_initial_state", yalkCheckInitialSafe);
    engine.registerProcedure("yalk.safe_cleanup", yalkSafeCleanupSafe);
}

} // namespace orbita::stand
