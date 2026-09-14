#include "orbita_stand/ubsi_procedures.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
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

bool responseBool(const std::string& response, const std::string& key)
{
    return responseUnsigned(response, key) != 0;
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

struct LogicalBinding {
    std::string source;
    std::string locatorType;
    std::string locator;
    std::string streamId;
    unsigned wordIndex = 0;
    unsigned mask = 0xFFFF;
    unsigned shift = 0;
    unsigned mode = 0;
    std::string conversionId;
    std::string stimulusRoute;
    unsigned stimulusOffset = 0;
    bool confirmed = false;
};

LogicalBinding resolveLogicalBinding(
    ProcedureContext& context, const std::string& parameterGroup, unsigned channel)
{
    const auto values = responseValues(context.equipment.invoke(
        "catalog.parameter_resolver", "resolve", {
            {"block_type", "UBSI_468157_002"},
            {"parameter_group", parameterGroup},
            {"channel_index", std::to_string(channel)}}));
    const auto required = [&values, &parameterGroup](const char* key) -> const std::string& {
        const auto value = values.find(key);
        if (value == values.end()) {
            throw std::runtime_error("Каталог не вернул " + std::string(key)
                + " для " + parameterGroup);
        }
        return value->second;
    };

    LogicalBinding result;
    result.source = required("source");
    result.locatorType = required("locator_type");
    result.locator = required("locator");
    result.streamId = required("stream_id");
    result.wordIndex = static_cast<unsigned>(std::stoul(required("word_index")));
    result.mask = static_cast<unsigned>(std::stoul(required("mask")));
    result.shift = static_cast<unsigned>(std::stoul(required("shift")));
    result.mode = static_cast<unsigned>(std::stoul(required("mode")));
    result.conversionId = required("conversion_id");
    result.stimulusRoute = required("stimulus_route");
    result.stimulusOffset = static_cast<unsigned>(std::stoul(required("stimulus_offset")));
    result.confirmed = required("confirmed") == "true";
    return result;
}

bool yalkBindingsConfirmed(ProcedureContext& context, unsigned count)
{
    for (unsigned channel = 0; channel < count; ++channel) {
        const auto voltage = resolveLogicalBinding(context, "yalk_voltage", channel);
        const auto signal = resolveLogicalBinding(context, "yalk_signal", channel);
        if (!voltage.confirmed || !signal.confirmed || voltage.locator.empty()
            || voltage.stimulusRoute.empty() || voltage.locator != signal.locator) return false;
    }
    return resolveLogicalBinding(context, "yalk_calibration_zero", 0).confirmed
        && resolveLogicalBinding(context, "yalk_calibration_full", 0).confirmed;
}

void markCommissioning(ProcedureResult& result, bool confirmed)
{
    if (!confirmed && result.verdict == RunVerdict::Ok) {
        result.verdict = RunVerdict::Incomplete;
        result.message += "; измерения выполнены в режиме наладки, адреса ещё не подтверждены";
    }
}

double readReferenceVoltage(ProcedureContext& context)
{
    return responseNumber(context.equipment.invoke(
        "measure.reference_voltage", "read_voltage", {}), "volts");
}

struct UlkChannelValue {
    double raw = 0.0;
    double code = 0.0;
    bool signal = false;
    unsigned firstSequence = 0;
    unsigned lastSequence = 0;
    std::string rawSamples;
    std::string codeSamples;
};

unsigned ulkLastSequence(ProcedureContext& context)
{
    return responseUnsigned(context.equipment.invoke(
        "ulk.parameter_source", "stats", {}), "last_sequence");
}

void publishBackground(ProcedureContext& context,
                       const std::map<std::string, std::string>& values)
{
    if (!values.count("background_mean")) return;
    if (!context.state.count("yalk.zero_code") || !context.state.count("yalk.full_code")) return;
    const double zero = std::stod(context.state.at("yalk.zero_code"));
    const double full = std::stod(context.state.at("yalk.full_code"));
    if (!(full > zero)) return;

    std::map<std::string, std::string> data{{"section", "YALK"}};
    for (const auto* key : {"background_mean", "background_min", "background_max"}) {
        const auto found = values.find(key);
        if (found == values.end()) return;
        std::istringstream input(found->second);
        std::ostringstream output;
        output << std::setprecision(10);
        std::string token;
        bool first = true;
        while (std::getline(input, token, ',')) {
            if (token.empty()) continue;
            if (!first) output << ',';
            output << (std::stod(token) - zero) * 6.2 / (full - zero);
            first = false;
        }
        data[key] = output.str();
    }
    context.eventSink({std::chrono::system_clock::now(), "monitor", "BACKGROUND",
        "Колебания всех каналов; диагностическая выборка", RunVerdict::NotRun, data});
}

UlkChannelValue readUlkChannel(
    ProcedureContext& context, unsigned address, unsigned samples, unsigned afterSequence)
{
    const auto response = context.equipment.invoke("ulk.parameter_source", "read_channel", {
        {"ulk_address", std::to_string(address)},
        {"sample_count", std::to_string(samples)},
        {"after_sequence", std::to_string(afterSequence)},
        {"timeout_ms", "3000"}});
    const auto values = responseValues(response);
    publishBackground(context, values);
    return {
        responseNumber(response, "raw_mean"),
        responseNumber(response, "analog_code_mean"),
        responseBool(response, "signal"),
        responseUnsigned(response, "first_sequence"),
        responseUnsigned(response, "last_sequence"),
        values.count("raw_samples") ? values.at("raw_samples") : std::string(),
        values.count("analog_code_samples") ? values.at("analog_code_samples") : std::string()};
}

double stateNumber(const ProcedureContext& context, const std::string& key)
{
    const auto found = context.state.find(key);
    if (found == context.state.end())
        throw std::runtime_error("Нет состояния сценария ЯЛК: " + key);
    return std::stod(found->second);
}

std::string scaledSamples(const std::string& samples, double zero, double full,
                          double fullScale)
{
    if (samples.empty() || !(full > zero)) return {};
    std::istringstream input(samples);
    std::ostringstream output;
    output << std::setprecision(10);
    std::string token;
    bool first = true;
    while (std::getline(input, token, ',')) {
        if (token.empty()) continue;
        if (!first) output << ',';
        output << (std::stod(token) - zero) * fullScale / (full - zero);
        first = false;
    }
    return output.str();
}

void setYalkVoltage(ProcedureContext& context, const LogicalBinding& binding,
                    double volts, bool enabled)
{
    const std::map<std::string, std::string> arguments{
        {"route", binding.stimulusRoute},
        {"ulk_address", binding.locator}};
    if (enabled) {
        auto voltageArguments = arguments;
        voltageArguments["volts"] = std::to_string(volts);
        context.equipment.invoke("stand.switch_matrix", "yalk_set_voltage", voltageArguments);
    } else {
        context.equipment.invoke("stand.switch_matrix", "yalk_output_off", arguments);
    }
}

double yalkCodeToVolts(double code, const ProcedureContext& context)
{
    const double zero = stateNumber(context, "yalk.zero_code");
    const double full = stateNumber(context, "yalk.full_code");
    const double voltage = stateNumber(context, "yalk.full_voltage");
    if (!(full > zero)) throw std::runtime_error("Неверная калибровка ЯЛК");
    return (code - zero) * voltage / (full - zero);
}

ProcedureResult yalkCheckChannelsPointMajor(const ScenarioNode& node, ProcedureContext& context)
{
    const unsigned count = natural(node, "channel_count", 80);
    const bool confirmed = yalkBindingsConfirmed(context, count);
    if (!confirmed && argument(node, "commissioning", "false") != "true") {
        return {RunVerdict::Incomplete,
            "Адреса/маршруты ЯЛК не подтверждены", {}};
    }

    const auto pointVolts = numbers(node, "point_volts");
    const auto signalExpectations = numbers(node, "signal_expectations");
    if (pointVolts.empty()
        || (signalExpectations.empty() && pointVolts.size() != 3)
        || (!signalExpectations.empty() && signalExpectations.size() != pointVolts.size())) {
        throw std::invalid_argument(
            "Для ЯЛК нужны три штатные точки либо signal_expectations для каждой точки");
    }

    const double fullScale = number(node, "full_scale_v", 6.2);
    const double tolerance = fullScale * number(node, "tolerance_percent_fs", 0.5) / 100.0;
    const unsigned sampleCount = natural(node, "sample_count", 16);
    const unsigned settleMs = natural(node, "settle_ms", 150);
    const unsigned offSettleMs = natural(node, "channel_off_settle_ms", 1000);

    std::vector<LogicalBinding> bindings;
    bindings.reserve(count);
    for (unsigned channel = 0; channel < count; ++channel)
        bindings.push_back(resolveLogicalBinding(context, "yalk_voltage", channel));

    std::vector<bool> contactPassed(count, true);
    ProcedureResult result{RunVerdict::Ok,
        "Проверены аналоговые значения и контактные сигналы ЯЛК в порядке по точкам", {}};

    // Physical order is point-major by design: first all channels at the first
    // point, then all channels at the next point. This is the operator-visible
    // sequence and is intentionally different from the historical channel-major
    // implementation.
    for (std::size_t point = 0; point < pointVolts.size(); ++point) {
        for (unsigned channel = 0; channel < count; ++channel) {
            const auto& binding = bindings[channel];
            const unsigned address = static_cast<unsigned>(std::stoul(binding.locator));
            bool outputEnabled = false;
            try {
                setYalkVoltage(context, binding, pointVolts[point], true);
                outputEnabled = true;
                wait(context, settleMs);

                const double v7 = readReferenceVoltage(context);
                const unsigned sequence = ulkLastSequence(context);
                const auto reading = readUlkChannel(context, address, sampleCount, sequence);
                const double volts = yalkCodeToVolts(reading.code, context);
                const double absolute = std::abs(volts - v7);
                const double reduced = absolute / fullScale * 100.0;

                auto analog = measurement(
                    "ubsi.yalk." + binding.locator + "." + std::to_string(point),
                    "ЯЛК адрес " + binding.locator + ", "
                        + std::to_string(pointVolts[point]) + " В",
                    v7, volts, v7 - tolerance, v7 + tolerance, "В");
                analog.attributes = {
                    {"ulk_address", binding.locator},
                    {"isd_channel", binding.locator},
                    {"command_v", std::to_string(pointVolts[point])},
                    {"point_index", std::to_string(point + 1)},
                    {"point_count", std::to_string(pointVolts.size())},
                    {"channel_index", std::to_string(channel + 1)},
                    {"channel_count", std::to_string(count)},
                    {"scan_order", "point_major"},
                    {"raw", std::to_string(reading.raw)},
                    {"analog_code", std::to_string(reading.code)},
                    {"raw_samples", reading.rawSamples},
                    {"analog_code_samples", reading.codeSamples},
                    {"value_samples", scaledSamples(reading.codeSamples,
                        stateNumber(context, "yalk.zero_code"),
                        stateNumber(context, "yalk.full_code"),
                        stateNumber(context, "yalk.full_voltage"))},
                    {"sample_count", std::to_string(sampleCount)},
                    {"signal", reading.signal ? "1" : "0"},
                    {"v7_v", std::to_string(v7)},
                    {"yalk_v", std::to_string(volts)},
                    {"absolute_error_v", std::to_string(absolute)},
                    {"lower_limit_v", std::to_string(analog.lowerLimit)},
                    {"upper_limit_v", std::to_string(analog.upperLimit)},
                    {"reduced_error_percent", std::to_string(reduced)},
                    {"relative_error_percent", std::abs(v7) > 0.01
                        ? std::to_string(absolute / std::abs(v7) * 100.0) : ""}};
                const auto verdict = analog.verdict;
                context.eventSink({std::chrono::system_clock::now(), node.id, "MEASUREMENT",
                    analog.title, verdict, analog.attributes});
                append(result, std::move(analog));

                const bool checkSignal = !signalExpectations.empty()
                    || point == 0 || point + 1 == pointVolts.size();
                if (checkSignal) {
                    const bool expected = !signalExpectations.empty()
                        ? signalExpectations[point] >= 0.5
                        : point + 1 == pointVolts.size();
                    auto signal = measurement(
                        "ubsi.yalk.signal." + binding.locator + "." + std::to_string(point),
                        "ЯЛК адрес " + binding.locator + ": контактный сигнал при "
                            + std::to_string(pointVolts[point]) + " В",
                        expected ? 1.0 : 0.0, reading.signal ? 1.0 : 0.0,
                        expected ? 1.0 : 0.0, expected ? 1.0 : 0.0, "лог.");
                    signal.attributes = {
                        {"ulk_address", binding.locator},
                        {"command_v", std::to_string(pointVolts[point])},
                        {"point_index", std::to_string(point + 1)},
                        {"channel_index", std::to_string(channel + 1)},
                        {"scan_order", "point_major"},
                        {"signal", reading.signal ? "1" : "0"}};
                    contactPassed[channel] = contactPassed[channel]
                        && signal.verdict == RunVerdict::Ok;
                    append(result, std::move(signal));
                }

                setYalkVoltage(context, binding, 0.0, false);
                outputEnabled = false;
                wait(context, offSettleMs);
            } catch (...) {
                if (outputEnabled) {
                    try { setYalkVoltage(context, binding, 0.0, false); } catch (...) {}
                }
                throw;
            }
        }
    }

    unsigned contactChannelsPassed = 0;
    for (const bool passed : contactPassed) {
        if (passed) ++contactChannelsPassed;
    }
    auto coverage = measurement("ubsi.yalk.contacts.coverage",
        "Контактные каналы ЯЛК, прошедшие состояния 0 и 1",
        static_cast<double>(count), static_cast<double>(contactChannelsPassed),
        static_cast<double>(count), static_cast<double>(count), "каналов");
    coverage.attributes = {
        {"required_by_tu_minimum", "30"},
        {"tested_channels", std::to_string(count)},
        {"passed_channels", std::to_string(contactChannelsPassed)},
        {"logic_0_test_voltage_v", std::to_string(pointVolts.front())},
        {"logic_1_test_voltage_v", std::to_string(pointVolts.back())},
        {"scan_order", "point_major"}};
    append(result, std::move(coverage));
    markCommissioning(result, confirmed);
    return result;
}

} // namespace

void registerPointMajorYalkProcedures(ScenarioEngine& engine)
{
    engine.registerProcedure("yalk.check_channels", yalkCheckChannelsPointMajor);
}

} // namespace orbita::stand
