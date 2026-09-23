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
#include <utility>
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

std::vector<unsigned> legacyCountOrSafeMap(
    const ScenarioNode& node, const std::string& listKey, const std::string& countKey)
{
    const auto explicitList = argument(node, listKey);
    if (!explicitList.empty()) return addressList(explicitList);

    // Compatibility with the current published scenario and small unit fixtures.
    // The historical value 88 never means "drive every ISD line": eight of those
    // lines (29/30/31/44/71/72/73/88) belong to the YVP wiring. Small synthetic
    // counts remain available so regression tests can exercise the algorithm.
    const unsigned legacyCount = natural(node, countKey, 88);
    if (legacyCount < 80) {
        std::vector<unsigned> result;
        result.reserve(legacyCount);
        for (unsigned value = 1; value <= legacyCount; ++value) result.push_back(value);
        return result;
    }
    return addressList(kSafeYalkAddresses);
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
            codeSums[index] += words[index] & 0x03FFu;
            if ((words[index] & 0x0400u) != 0) ++signalOnes[index];
        }
    }

    std::vector<SnapshotValue> result(100);
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index].code = codeSums[index] / samples;
        result[index].signal = signalOnes[index] * 2 >= samples;
    }
    return result;
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

void publish(ProcedureContext& context, const ScenarioNode& node,
             const std::string& stage, const std::string& message,
             RunVerdict verdict, std::map<std::string, std::string> data = {})
{
    if (!context.eventSink) return;
    context.eventSink({std::chrono::system_clock::now(), node.id, stage,
        message, verdict, std::move(data)});
}

ProcedureResult yalkOverloadPhysical(const ScenarioNode& node, ProcedureContext& context)
{
    if (argument(node, "mapping_confirmed", "false") != "true") {
        return {RunVerdict::Incomplete,
            "Маршруты ±12 В не подтверждены; опасное воздействие не выполнялось", {}};
    }

    const auto safeMap = addressList(kSafeYalkAddresses);
    const auto physical = legacyCountOrSafeMap(
        node, "physical_channels", "physical_channel_count");
    const auto stressed = argument(node, "stressed_channels").empty()
        ? physical : addressList(argument(node, "stressed_channels"));
    const auto observed = legacyCountOrSafeMap(
        node, "observed_addresses", "observed_address_count");

    if (physical.empty() || stressed.empty() || observed.empty())
        throw std::invalid_argument("Карта перегрузки ЯЛК не может быть пустой");
    if (physical.size() >= 80 && physical != safeMap)
        throw std::invalid_argument(
            "Полная перегрузка ЯЛК разрешена только для подтверждённой безопасной карты 80 каналов");
    if (observed.size() >= 80 && observed != safeMap)
        throw std::invalid_argument(
            "Полное наблюдение ЯЛК разрешено только для подтверждённой безопасной карты 80 адресов");
    for (const unsigned target : stressed) {
        if (std::find(physical.begin(), physical.end(), target) == physical.end())
            throw std::invalid_argument("Перегружаемый канал отсутствует в безопасной карте ЯЛК");
    }

    const unsigned samples = natural(node, "sample_count", 4);
    const unsigned baselineSettle = natural(node, "baseline_settle_ms", 1000);
    const unsigned dacOffSettle = natural(node, "dac_off_settle_ms", 300);
    const unsigned overloadSettle = natural(node, "overload_settle_ms", 10000);
    const unsigned cleanupSettle = natural(node, "cleanup_settle_ms", 300);
    const unsigned maximumCodeDelta = natural(node, "maximum_code_delta", 2);
    const std::string positiveRoute = argument(
        node, "positive_overload_route", "yalk_overload_positive");
    const std::string negativeRoute = argument(
        node, "negative_overload_route", "yalk_overload_negative");
    const std::string backgroundOwner = "run:" + context.runId + ":yalk-overload-bg:" + node.id;
    const std::string impactOwner = "run:" + context.runId + ":yalk-overload-impact:" + node.id;

    const auto analogCode = [](unsigned channel) {
        return channel <= 10 ? 780u + (channel - 1) * 30u
                             : 1800u + (channel - 11) * 20u;
    };
    const auto setAnalog = [&](unsigned channel, unsigned code, bool enabled) {
        context.equipment.invoke("stand.switch_matrix", "analog", {
            {"channel", std::to_string(channel)},
            {"code", std::to_string(code)},
            {"enabled", enabled ? "true" : "false"},
            {"owner", backgroundOwner}});
    };
    const auto setSwitch = [&](std::map<std::string, std::string> arguments) {
        arguments["type"] = "3";
        arguments["owner"] = impactOwner;
        context.equipment.invoke("stand.switch_matrix", "switch", arguments);
    };
    const auto release = [&](const std::string& owner) {
        context.equipment.invoke("stand.switch_matrix", "release_owner", {
            {"owner", owner}});
    };
    const auto releaseImpact = [&] { release(impactOwner); };
    const auto sourceOff = [&](const std::string& route) {
        try { setSwitch({{"route", route}, {"enabled", "false"}}); } catch (...) {}
    };
    const auto staircaseOn = [&] {
        for (const unsigned channel : physical)
            setAnalog(channel, analogCode(channel), true);
    };
    const auto staircaseOff = [&] {
        for (auto item = physical.rbegin(); item != physical.rend(); ++item) {
            try { setAnalog(*item, 0, false); } catch (...) {}
        }
    };
    const auto finalCleanup = [&] {
        try { releaseImpact(); } catch (...) {}
        sourceOff(positiveRoute);
        sourceOff(negativeRoute);
        staircaseOff();
        try { release(backgroundOwner); } catch (...) {}
    };

    ProcedureResult result{RunVerdict::Ok,
        stressed.size() == physical.size()
            ? "Проверена устойчивость остальных каналов ЯЛК при перегрузке ±12 В"
            : "Проверена выборочная перегрузка ЯЛК ±12 В", {}};

    try {
        // Separate owners let us clear transient ±12 V switches after every
        // impact while keeping all unaffected DAC background outputs alive.
        release(backgroundOwner);
        releaseImpact();
        staircaseOn();
        waitScaled(context, baselineSettle);
        const auto baseline = readFreshSnapshot(context, samples);
        publish(context, node, "OVERLOAD_BASELINE", "Снят свежий baseline ЯЛК",
            RunVerdict::NotRun,
            {{"physical_count", std::to_string(physical.size())},
             {"observed_count", std::to_string(observed.size())},
             {"safe_map", kSafeYalkAddresses}});

        unsigned impactIndex = 0;
        const unsigned impactCount = static_cast<unsigned>(stressed.size() * 2);
        for (const auto& polarity : std::vector<std::pair<std::string, std::string>>{
                 {positiveRoute, "+12 В"}, {negativeRoute, "-12 В"}}) {
            for (const unsigned target : stressed) {
                if (context.stopRequested.load())
                    throw std::runtime_error("Остановлено оператором");
                ++impactIndex;
                publish(context, node, "OVERLOAD",
                    "ЯЛК: перегрузка " + polarity.second + ", канал "
                        + std::to_string(target),
                    RunVerdict::NotRun,
                    {{"polarity", polarity.second},
                     {"stressed_channel", std::to_string(target)},
                     {"impact_index", std::to_string(impactIndex)},
                     {"impact_count", std::to_string(impactCount)},
                     {"dac_off_settle_ms", std::to_string(dacOffSettle)},
                     {"overload_settle_ms", std::to_string(overloadSettle)}});

                bool targetDacRemoved = false;
                bool commonEnabled = false;
                bool targetConnected = false;
                try {
                    // Confirmed donor order: leave the other background DACs
                    // active, remove only the target DAC, then connect ±12 V.
                    setAnalog(target, 0, false);
                    targetDacRemoved = true;
                    waitScaled(context, dacOffSettle);
                    setSwitch({{"route", polarity.first}, {"enabled", "true"}});
                    commonEnabled = true;
                    setSwitch({{"channel", std::to_string(target)}, {"enabled", "true"}});
                    targetConnected = true;
                    waitScaled(context, overloadSettle);

                    const auto current = readFreshSnapshot(context, samples);
                    for (const unsigned address : observed) {
                        if (address == target) continue;
                        if (!address || address > baseline.size() || address > current.size())
                            throw std::runtime_error("Адрес наблюдения ЯЛК вне snapshot");
                        const double base = baseline[address - 1].code;
                        const double now = current[address - 1].code;
                        const double delta = now - base;
                        auto value = measurement(
                            "ubsi.yalk.overload." + polarity.first + "."
                                + std::to_string(target) + "." + std::to_string(address),
                            "ЯЛК: " + polarity.second + " на " + std::to_string(target)
                                + ", наблюдение " + std::to_string(address),
                            base, now, base - maximumCodeDelta,
                            base + maximumCodeDelta, "код");
                        value.attributes = {
                            {"polarity", polarity.second},
                            {"stressed_channel", std::to_string(target)},
                            {"observed_channel", std::to_string(address)},
                            {"baseline_code", std::to_string(base)},
                            {"current_code", std::to_string(now)},
                            {"delta_code", std::to_string(delta)},
                            {"lower_delta_code", std::to_string(-static_cast<int>(maximumCodeDelta))},
                            {"upper_delta_code", std::to_string(maximumCodeDelta)},
                            {"impact_index", std::to_string(impactIndex)},
                            {"impact_count", std::to_string(impactCount)}};
                        publish(context, node, "MEASUREMENT", value.title,
                            value.verdict, value.attributes);
                        append(result, std::move(value));
                    }

                    // Explicit reverse transition is retained in evidence;
                    // releaseImpact is the ownership/failure-safety barrier.
                    setSwitch({{"channel", std::to_string(target)}, {"enabled", "false"}});
                    targetConnected = false;
                    setSwitch({{"route", polarity.first}, {"enabled", "false"}});
                    commonEnabled = false;
                    releaseImpact();
                    setAnalog(target, analogCode(target), true);
                    targetDacRemoved = false;
                    waitScaled(context, cleanupSettle);
                } catch (...) {
                    if (targetConnected) {
                        try { setSwitch({{"channel", std::to_string(target)}, {"enabled", "false"}}); }
                        catch (...) {}
                    }
                    if (commonEnabled) sourceOff(polarity.first);
                    try { releaseImpact(); } catch (...) {}
                    if (targetDacRemoved) {
                        try { setAnalog(target, analogCode(target), true); } catch (...) {}
                    }
                    finalCleanup();
                    throw;
                }
            }
        }

        finalCleanup();
        return result;
    } catch (...) {
        finalCleanup();
        throw;
    }
}

} // namespace

void registerYalkPhysicalUbsiProcedures(ScenarioEngine& engine)
{
    engine.registerProcedure("yalk.check_overload", yalkOverloadPhysical);
}

} // namespace orbita::stand
