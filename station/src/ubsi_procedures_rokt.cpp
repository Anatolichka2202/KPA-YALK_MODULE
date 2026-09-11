#include "orbita_stand/ubsi_procedures.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

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
    engine.registerProcedure("yvp.enter_mode", yvpEnterMode);
    engine.registerProcedure("ubsi.yvp", yvpRoktChannels);
    engine.registerProcedure("yvp.safe_cleanup", yvpSafeCleanup);
}

} // namespace orbita::stand
