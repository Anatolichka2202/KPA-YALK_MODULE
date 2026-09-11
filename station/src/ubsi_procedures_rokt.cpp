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

unsigned valueOrZero(const std::map<std::string, std::string>& values,
                     const std::string& key)
{
    const auto found = values.find(key);
    if (found == values.end() || found->second.empty()) return 0;
    return static_cast<unsigned>(std::stoul(found->second));
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

    auto analogCode = [](unsigned channel) {
        return channel <= 10 ? 780u + (channel - 1) * 30u
                             : 1800u + (channel - 11) * 20u;
    };
    auto setAnalog = [&context](unsigned channel, unsigned code, bool enabled) {
        context.equipment.invoke("stand.switch_matrix", "analog", {
            {"channel", std::to_string(channel)}, {"code", std::to_string(code)},
            {"enabled", enabled ? "true" : "false"}});
    };
    auto setSwitch = [&context](std::map<std::string, std::string> args) {
        args["type"] = "3";
        context.equipment.invoke("stand.switch_matrix", "switch", args);
    };
    auto sourceOff = [&]() {
        for (const auto& route : {positiveRoute, negativeRoute}) {
            try { setSwitch({{"route", route}, {"enabled", "false"}}); } catch (...) {}
        }
    };
    auto dacOff = [&]() {
        for (unsigned channel = 1; channel <= physicalCount; ++channel) {
            try { setAnalog(channel, 0, false); } catch (...) {}
        }
    };
    auto makeSafe = [&]() {
        sourceOff();
        context.equipment.invoke("stand.switch_matrix", "full_reset", {});
        dacOff();
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

                bool targetConnected = false;
                try {
                    applyReferenceStaircase();
                    setSwitch({{"route", polarity.first}, {"enabled", "true"}});
                    setAnalog(target, 0, false);
                    setSwitch({{"channel", std::to_string(target)}, {"enabled", "true"}});
                    targetConnected = true;
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
                        append(result, std::move(value));
                    }
                    targetConnected = false;
                    makeSafe();
                } catch (...) {
                    if (targetConnected) {
                        try { setSwitch({{"channel", std::to_string(target)}, {"enabled", "false"}}); }
                        catch (...) {}
                    }
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

std::map<std::string, std::string> waitForYvpTraffic(
    ProcedureContext& context, unsigned timeoutMs)
{
    const auto started = std::chrono::steady_clock::now();
    do {
        if (context.stopRequested.load())
            throw std::runtime_error("Остановлено оператором");

        const auto values = responseValues(context.equipment.invoke(
            "ulk.parameter_source", "stats", {}));
        if (valueOrZero(values, "last_sequence") > 0) return values;

        const auto elapsed = static_cast<unsigned>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count());
        if (elapsed >= timeoutMs)
            throw std::runtime_error("ЯВП: после ROKT-команды не получено ни одного UDP-кадра");

        std::this_thread::sleep_for(std::chrono::milliseconds(
            std::min(50u, timeoutMs - elapsed)));
    } while (true);
}

std::map<std::string, std::string> trafficData(
    const std::map<std::string, std::string>& stats)
{
    std::map<std::string, std::string> data;
    for (const char* key : {"last_sequence", "service4", "fast120", "slow200",
                            "reference204", "ytp_legacy65", "ytp_rokt68",
                            "unknown", "dropped"}) {
        const auto found = stats.find(key);
        if (found != stats.end()) data[key] = found->second;
    }
    return data;
}

ProcedureResult yvpEnterMode(const ScenarioNode& node, ProcedureContext& context)
{
    const unsigned cell = natural(node, "yvp_cell", natural(node, "cell", 1));
    const unsigned timeoutMs = natural(node, "timeout_ms", 3000);
    if (cell < 1 || cell > 255)
        throw std::invalid_argument("ЯВП: номер ячейки должен быть 1..255");

    context.equipment.invoke("ulk.parameter_source", "start_yvp_probe", {
        {"cell", std::to_string(cell)}});

    const auto stats = waitForYvpTraffic(context, timeoutMs);
    auto data = trafficData(stats);
    data["cell"] = std::to_string(cell);
    data["active_command"] = "ROKT_0A_01";
    data["decoder"] = "unconfirmed";
    context.eventSink({std::chrono::system_clock::now(), node.id, "YVP_MODE",
        "ROKT 0A 01 отправлена; после переключения получен UDP-трафик ЯВП",
        RunVerdict::Ok, std::move(data)});

    return {RunVerdict::Ok,
        "Режим ЯВП включён командой ROKT 0A 01; UDP-трафик после переключения присутствует",
        {}};
}

ProcedureResult yvpRoktChannels(const ScenarioNode& node, ProcedureContext& context)
{
    const unsigned channels = natural(node, "channel_count", 8);
    const unsigned cell = natural(node, "yvp_cell", natural(node, "cell", 1));
    const unsigned timeoutMs = natural(node, "timeout_ms", 3000);
    if (channels != 8)
        throw std::invalid_argument("ЯВП-8: текущая подтверждённая ROKT-команда рассчитана на 8 каналов");
    if (cell < 1 || cell > 255)
        throw std::invalid_argument("ЯВП: номер ячейки должен быть 1..255");

    for (unsigned channel = 1; channel <= channels; ++channel) {
        context.equipment.invoke("ulk.parameter_source", "start_yvp_channel_probe", {
            {"channel", std::to_string(channel)},
            {"cell", std::to_string(cell)}});

        const auto stats = waitForYvpTraffic(context, timeoutMs);
        auto data = trafficData(stats);
        data["channel"] = std::to_string(channel);
        data["cell"] = std::to_string(cell);
        data["active_command"] = "ROKT_0A_03";
        data["wire_channel"] = std::to_string(channel - 1);
        data["decoder"] = "unconfirmed";
        context.eventSink({std::chrono::system_clock::now(), node.id, "YVP_CHANNEL",
            "Канал ЯВП " + std::to_string(channel)
                + ": ROKT 0A 03 отправлена, свежий UDP-трафик получен",
            RunVerdict::NotRun, std::move(data)});
    }

    return {RunVerdict::Incomplete,
        "ROKT-переключение всех 8 каналов ЯВП подтверждено трафиком. Формат полезной нагрузки, "
        "масштабирование и критерии приёмочного измерения пока не подтверждены; изделию не присваивается НОРМА/НЕ НОРМА",
        {}};
}

ProcedureResult yvpSafeCleanup(const ScenarioNode&, ProcedureContext& context)
{
    context.equipment.invoke("ulk.parameter_source", "stop_stream", {});
    return {RunVerdict::Ok, "Поток ЯВП остановлен", {}};
}

} // namespace

void registerUbsiProcedures(ScenarioEngine& engine)
{
    registerCurrentUbsiProcedures(engine);
    // Replace the legacy callback only to expose per-impact progress. The
    // measurement sequence, limits and safe-state behaviour stay identical.
    engine.registerProcedure("yalk.check_overload", yalkOverloadWithProgress);
    engine.registerProcedure("yvp.enter_mode", yvpEnterMode);
    engine.registerProcedure("ubsi.yvp", yvpRoktChannels);
    engine.registerProcedure("yvp.safe_cleanup", yvpSafeCleanup);
}

} // namespace orbita::stand
