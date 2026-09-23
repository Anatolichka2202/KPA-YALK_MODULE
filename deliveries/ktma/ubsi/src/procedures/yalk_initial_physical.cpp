#include "registration_layers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace orbita::stand {
namespace {

constexpr const char* kSafeYalkAddresses = "1-28,32-43,45-70,74-87";

std::string argument(const ScenarioNode& node, const std::string& key,
                     std::string fallback = {})
{
    const auto found = node.arguments.find(key);
    return found == node.arguments.end() ? std::move(fallback) : found->second;
}

unsigned natural(const ScenarioNode& node, const std::string& key, unsigned fallback)
{
    const auto text = argument(node, key);
    if (text.empty()) return fallback;
    std::size_t parsed = 0;
    const auto value = std::stoul(text, &parsed, 0);
    if (parsed != text.size())
        throw std::invalid_argument("Некорректный аргумент " + key);
    return static_cast<unsigned>(value);
}

double number(const ScenarioNode& node, const std::string& key, double fallback)
{
    const auto text = argument(node, key);
    if (text.empty()) return fallback;
    std::size_t parsed = 0;
    const double value = std::stod(text, &parsed);
    if (parsed != text.size() || !std::isfinite(value))
        throw std::invalid_argument("Некорректный аргумент " + key);
    return value;
}

std::vector<unsigned> addressList(const std::string& text)
{
    std::vector<unsigned> result;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (item.empty()) continue;
        const auto dash = item.find('-');
        if (dash == std::string::npos) {
            result.push_back(static_cast<unsigned>(std::stoul(item)));
            continue;
        }
        const unsigned first = static_cast<unsigned>(std::stoul(item.substr(0, dash)));
        const unsigned last = static_cast<unsigned>(std::stoul(item.substr(dash + 1)));
        if (!first || last < first)
            throw std::invalid_argument("Некорректный диапазон адресов ЯЛК: " + item);
        for (unsigned value = first; value <= last; ++value) result.push_back(value);
    }
    return result;
}

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

unsigned responseUnsigned(const std::string& response, const std::string& key)
{
    const auto values = responseValues(response);
    const auto found = values.find(key);
    if (found == values.end())
        throw std::runtime_error("Адаптер не вернул поле " + key);
    return static_cast<unsigned>(std::stoul(found->second));
}

std::vector<unsigned> commaSeparatedUnsigned(const std::string& text)
{
    std::vector<unsigned> result;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) result.push_back(static_cast<unsigned>(std::stoul(item)));
    }
    return result;
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
        if (context.stopRequested.load())
            throw std::runtime_error("Остановлено оператором");
        std::this_thread::sleep_for(std::chrono::milliseconds(
            std::min(slice, milliseconds - elapsed)));
    }
}

struct SnapshotValue {
    double code = 0.0;
    bool signal = false;
    unsigned raw = 0;
};

std::vector<SnapshotValue> readFreshSnapshot(
    ProcedureContext& context, unsigned sampleCount)
{
    unsigned sequence = responseUnsigned(
        context.equipment.invoke("ulk.parameter_source", "stats", {}),
        "last_sequence");
    const unsigned samples = std::max(1u, sampleCount);
    std::vector<double> codeSums(100, 0.0);
    std::vector<unsigned> signalOnes(100, 0);
    std::vector<unsigned> lastRaw(100, 0);

    for (unsigned sample = 0; sample < samples; ++sample) {
        const auto response = context.equipment.invoke(
            "ulk.parameter_source", "read_snapshot", {
                {"after_sequence", std::to_string(sequence)},
                {"timeout_ms", "3000"}});
        const auto values = responseValues(response);
        const auto wordsText = values.find("words");
        if (wordsText == values.end())
            throw std::runtime_error("Адаптер не вернул снимок ЯЛК");
        const auto words = commaSeparatedUnsigned(wordsText->second);
        if (words.size() < 100)
            throw std::runtime_error("В снимке ЯЛК меньше 100 слов");
        sequence = responseUnsigned(response, "sequence");
        for (std::size_t index = 0; index < 100; ++index) {
            lastRaw[index] = words[index];
            codeSums[index] += words[index] & 0x03FFu;
            if ((words[index] & 0x0400u) != 0) ++signalOnes[index];
        }
    }

    std::vector<SnapshotValue> result(100);
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index].raw = lastRaw[index];
        result[index].code = codeSums[index] / samples;
        result[index].signal = signalOnes[index] * 2 >= samples;
    }
    return result;
}

double stateNumber(const ProcedureContext& context, const std::string& key)
{
    const auto found = context.state.find(key);
    if (found == context.state.end())
        throw std::runtime_error("Нет состояния сценария ЯЛК: " + key);
    return std::stod(found->second);
}

double codeToVolts(double code, const ProcedureContext& context)
{
    const double zero = stateNumber(context, "yalk.zero_code");
    const double full = stateNumber(context, "yalk.full_code");
    const double fullVoltage = stateNumber(context, "yalk.full_voltage");
    if (!(full > zero)) throw std::runtime_error("Неверная калибровка ЯЛК");
    return (code - zero) * fullVoltage / (full - zero);
}

void publish(ProcedureContext& context, const ScenarioNode& node,
             const std::string& stage, const std::string& message,
             RunVerdict verdict, std::map<std::string, std::string> data = {})
{
    if (context.eventSink) context.eventSink({
        std::chrono::system_clock::now(), node.id, stage,
        message, verdict, std::move(data)});
}

ProcedureResult yalkInitialPhysical(const ScenarioNode& node, ProcedureContext& context)
{
    const auto addresses = addressList(argument(node, "addresses", kSafeYalkAddresses));
    if (addresses != addressList(kSafeYalkAddresses)) {
        throw std::invalid_argument(
            "Формальная проверка обрыва ЯЛК разрешена только по подтверждённой карте 80 адресов");
    }

    const unsigned requestedCount = natural(node, "channel_count", 80);
    if (requestedCount != addresses.size())
        throw std::invalid_argument("Число каналов ЯЛК не совпадает с подтверждённой картой");

    const unsigned samples = natural(node, "sample_count", 4);
    const unsigned settle = natural(node, "preclean_settle_ms", 300);
    const double fullScale = number(node, "full_scale_v", 6.2);
    const std::string owner = "run:" + context.runId + ":yalk-initial-cleanup:" + node.id;

    const auto analogOff = [&](unsigned channel) {
        context.equipment.invoke("stand.switch_matrix", "analog", {
            {"channel", std::to_string(channel)},
            {"code", "0"},
            {"enabled", "false"},
            {"owner", owner}});
    };
    const auto switchOff = [&](const std::string& route) {
        context.equipment.invoke("stand.switch_matrix", "switch", {
            {"type", "3"}, {"route", route}, {"enabled", "false"}, {"owner", owner}});
    };
    const auto release = [&] {
        context.equipment.invoke("stand.switch_matrix", "release_owner", {{"owner", owner}});
    };

    // The frozen proven implementation restores the ordinary YALK start state
    // by addressing known routes. Do not use the ISD firmware type=4 reset as
    // generic pre-cleanup: it is a service recovery primitive, not a measurement
    // step, and it destroys state outside the YALK ownership domain.
    for (const unsigned address : addresses) analogOff(address);
    switchOff("yalk_overload_positive");
    switchOff("yalk_overload_negative");
    waitScaled(context, settle);

    const auto frame = readFreshSnapshot(context, samples);
    ProcedureResult result{RunVerdict::Ok,
        "Проверено отключённое состояние и обрыв 80 входов ЯЛК", {}};

    for (std::size_t index = 0; index < addresses.size(); ++index) {
        const unsigned address = addresses[index];
        const auto& reading = frame.at(address - 1);
        const double volts = codeToVolts(reading.code, context);

        MeasurementResult value;
        value.parameterKey = "ubsi.yalk.initial." + std::to_string(address);
        value.title = "ЯЛК адрес " + std::to_string(address) + ": обрыв";
        value.reference = 0.0;
        value.measured = volts;
        value.lowerLimit = -fullScale;
        value.upperLimit = -1e-12;
        value.unit = "В";
        value.verdict = std::isfinite(volts) && volts < 0.0
            ? RunVerdict::Ok : RunVerdict::Fail;
        value.message = value.verdict == RunVerdict::Ok
            ? std::string() : "При обрыве значение ЯЛК должно быть ниже 0 В";
        value.attributes = {
            {"channel_index", std::to_string(index + 1)},
            {"channel_count", std::to_string(addresses.size())},
            {"ulk_address", std::to_string(address)},
            {"raw", std::to_string(reading.raw)},
            {"analog_code", std::to_string(reading.code)},
            {"yalk_v", std::to_string(volts)},
            {"signal", reading.signal ? "1" : "0"},
            {"signal_check", "diagnostic_only"},
            {"fresh", "true"}};

        publish(context, node, "YALK_INITIAL", value.title,
            value.verdict, value.attributes);
        result.verdict = combineVerdicts(result.verdict, value.verdict);
        result.measurements.push_back(std::move(value));
    }

    try { release(); } catch (...) {}
    return result;
}

} // namespace

void registerYalkInitialPhysicalUbsiProcedures(ScenarioEngine& engine)
{
    engine.registerProcedure("yalk.check_initial_state", yalkInitialPhysical);
}

} // namespace orbita::stand
