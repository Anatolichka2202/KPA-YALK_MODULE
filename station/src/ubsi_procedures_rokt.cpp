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
#include <vector>

namespace orbita::stand {

void registerCurrentUbsiProcedures(ScenarioEngine& engine);

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

unsigned natural(const ScenarioNode& node, const std::string& key, unsigned fallback)
{
    const auto text = argument(node, key);
    if (text.empty()) return fallback;
    std::size_t parsed = 0;
    const auto value = std::stoul(text, &parsed, 0);
    if (parsed != text.size()) throw std::invalid_argument("Некорректный аргумент " + key);
    return static_cast<unsigned>(value);
}

unsigned responseUnsigned(const std::string& response, const std::string& key)
{
    const auto values = responseValues(response);
    const auto found = values.find(key);
    if (found == values.end()) throw std::runtime_error("Адаптер не вернул поле " + key);
    return static_cast<unsigned>(std::stoul(found->second));
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

struct YalkSnapshotValue {
    double code = 0.0;
    bool signal = false;
};

std::vector<YalkSnapshotValue> readFreshYalkSnapshot(
    ProcedureContext& context, unsigned sampleCount)
{
    const auto stats = context.equipment.invoke("ulk.parameter_source", "stats", {});
    unsigned sequence = responseUnsigned(stats, "last_sequence");
    std::vector<double> codeSums(100, 0.0);
    std::vector<unsigned> signalOnes(100, 0);
    const unsigned samples = std::max(1u, sampleCount);
    for (unsigned sample = 0; sample < samples; ++sample) {
        const auto response = context.equipment.invoke("ulk.parameter_source", "read_snapshot", {
            {"after_sequence", std::to_string(sequence)}, {"timeout_ms", "3000"}});
        const auto values = responseValues(response);
        const auto wordsText = values.find("words");
        if (wordsText == values.end()) throw std::runtime_error("Адаптер не вернул снимок ЯЛК");
        const auto words = commaSeparatedUnsigned(wordsText->second);
        if (words.size() < 100) throw std::runtime_error("В снимке ЯЛК меньше 100 слов");
        sequence = responseUnsigned(response, "sequence");
        for (std::size_t index = 0; index < 100; ++index) {
            codeSums[index] += words[index] & 0x03FFu;
            if ((words[index] & 0x0400u) != 0) ++signalOnes[index];
        }
    }
    std::vector<YalkSnapshotValue> result(100);
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index].code = codeSums[index] / samples;
        result[index].signal = signalOnes[index] * 2 >= samples;
    }
    return result;
}

ProcedureResult yalkOverloadWithProgress(const ScenarioNode& node, ProcedureContext& context)
{
    if (argument(node, "mapping_confirmed", "false") != "true") {
        return {RunVerdict::Incomplete,
            "Маршруты обрыва и ±12 В ещё не подтверждены на УБСИ; опасное воздействие не выполнялось", {}};
    }

    const unsigned physicalCount = natural(node, "physical_channel_count", 88);
    const unsigned observedCount = natural(node, "observed_address_count", 88);
    const unsigned samples = natural(node, "sample_count", 4);
    const unsigned overloadSettle = natural(node, "overload_settle_ms", 10000);
    const unsigned cleanupSettle = natural(node, "cleanup_settle_ms", 300);
    const unsigned maximumCodeDelta = natural(node, "maximum_code_delta", 2);
    const std::string positiveRoute = argument(
        node, "positive_overload_route", "yalk_overload_positive");
    const std::string negativeRoute = argument(
        node, "negative_overload_route", "yalk_overload_negative");
    const std::string isdOwner = "run:" + context.runId + ":yalk-overload:" + node.id;

    auto analogCode = [](unsigned channel) {
        return channel <= 10 ? 780u + (channel - 1) * 30u
                             : 1800u + (channel - 11) * 20u;
    };
    auto setAnalog = [&](unsigned channel, unsigned code, bool enabled) {
        context.equipment.invoke("stand.switch_matrix", "analog", {
            {"channel", std::to_string(channel)}, {"code", std::to_string(code)},
            {"enabled", enabled ? "true" : "false"}, {"owner", isdOwner}});
    };
    auto setSwitch = [&](std::map<std::string, std::string> args) {
        args["type"] = "3";
        args["owner"] = isdOwner;
        context.equipment.invoke("stand.switch_matrix", "switch", args);
    };
    auto makeSafe = [&]() {
        // Do not use firmware type=4 here. Every route activated by this
        // procedure carries one owner; releaseOwner sends targeted OFF in
        // reverse order, including possibly-active commands that lost ACK.
        context.equipment.invoke("stand.switch_matrix", "release_owner", {
            {"owner", isdOwner}});
        waitScaled(context, cleanupSettle);
    };
    auto applyReferenceStaircase = [&]() {
        for (unsigned channel = 1; channel <= physicalCount; ++channel)
            setAnalog(channel, analogCode(channel), true);
    };

    ProcedureResult result{RunVerdict::Ok,
        "Проверена устойчивость остальных каналов ЯЛК при перегрузке ±12 В", {}};
    try {
        makeSafe();
        applyReferenceStaircase();
        waitScaled(context, natural(node, "baseline_settle_ms", 1000));
        const auto baseline = readFreshYalkSnapshot(context, samples);
        makeSafe();

        unsigned impactIndex = 0;
        const unsigned impactCount = physicalCount * 2;
        for (const auto& polarity : std::vector<std::pair<std::string, std::string>>{
                 {positiveRoute, "+12 В"}, {negativeRoute, "-12 В"}}) {
            for (unsigned target = 1; target <= physicalCount; ++target) {
                ++impactIndex;
                context.eventSink({std::chrono::system_clock::now(), node.id, "OVERLOAD",
                    "ЯЛК: перегрузка " + polarity.second + ", канал "
                        + std::to_string(target) + " из " + std::to_string(physicalCount),
                    RunVerdict::NotRun,
                    {{"polarity", polarity.second},
                     {"stressed_channel", std::to_string(target)},
                     {"target_count", std::to_string(physicalCount)},
                     {"impact_index", std::to_string(impactIndex)},
                     {"impact_count", std::to_string(impactCount)},
                     {"settle_ms", std::to_string(overloadSettle)}}});

                try {
                    applyReferenceStaircase();
                    setSwitch({{"route", polarity.first}, {"enabled", "true"}});
                    setAnalog(target, 0, false);
                    setSwitch({{"channel", std::to_string(target)}, {"enabled", "true"}});
                    waitScaled(context, overloadSettle);
                    const auto current = readFreshYalkSnapshot(context, samples);

                    for (unsigned address = 1;
                         address <= std::min<unsigned>(observedCount, current.size()); ++address) {
                        if (address == target) continue;
                        const double baselineCode = baseline[address - 1].code;
                        const double currentCode = current[address - 1].code;
                        const double delta = currentCode - baselineCode;
                        auto value = measurement(
                            "ubsi.yalk.overload." + polarity.first + "."
                                + std::to_string(target) + "." + std::to_string(address),
                            "ЯЛК: " + polarity.second + " на " + std::to_string(target)
                                + ", наблюдаемый канал " + std::to_string(address),
                            baselineCode, currentCode,
                            baselineCode - maximumCodeDelta,
                            baselineCode + maximumCodeDelta, "код");
                        value.attributes = {{"polarity", polarity.second},
                            {"stressed_channel", std::to_string(target)},
                            {"observed_channel", std::to_string(address)},
                            {"baseline_code", std::to_string(baseline[address - 1].code)},
                            {"current_code", std::to_string(current[address - 1].code)},
                            {"delta_code", std::to_string(delta)},
                            {"lower_delta_code", std::to_string(-static_cast<int>(maximumCodeDelta))},
                            {"upper_delta_code", std::to_string(maximumCodeDelta)}};
                        context.eventSink({std::chrono::system_clock::now(), node.id,
                            "MEASUREMENT", value.title, value.verdict, value.attributes});
                        append(result, std::move(value));
                    }
                    makeSafe();
                } catch (...) {
                    try { makeSafe(); } catch (...) {}
                    throw;
                }
            }
        }
        makeSafe();
    } catch (...) {
        try { makeSafe(); } catch (...) {}
        throw;
    }
    return result;
}

} // namespace

void registerUbsiProcedures(ScenarioEngine& engine)
{
    registerCurrentUbsiProcedures(engine);
    // Replace the legacy callback only to expose per-impact progress while
    // keeping ISD cleanup targeted to the current procedure owner.
    engine.registerProcedure("yalk.check_overload", yalkOverloadWithProgress);
}

} // namespace orbita::stand
