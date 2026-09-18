#include "orbita_stand/ubsi_procedures.h"

#include <algorithm>
#include <chrono>
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

ProcedureResult isdBaseline(const ScenarioNode& node, ProcedureContext& context)
{
    const std::string owner = "run:" + context.runId + ":bootstrap:" + node.id;

    // This is the only normal production use of firmware type=4.  The plugin
    // sends exactly one service_full_reset request with its dedicated service
    // timeout.  Any timeout/error propagates and stops the scenario traversal.
    context.equipment.invoke("stand.switch_matrix", "service_full_reset", {
        {"owner", owner}});

    const auto state = responseValues(context.equipment.invoke(
        "stand.switch_matrix", "state", {}));
    const auto session = state.find("session_state");
    const auto owned = state.find("owned_count");
    if (session == state.end() || owned == state.end()) {
        throw std::runtime_error(
            "ИСД после baseline не вернул session_state/owned_count");
    }
    if (session->second != "operational" || owned->second != "0") {
        throw std::runtime_error(
            "ИСД после baseline не перешёл в operational с owned_count=0");
    }

    MeasurementResult baseline;
    baseline.parameterKey = "stand.isd.baseline";
    baseline.title = "Команда стартового all-off baseline ИСД";
    baseline.reference = 0.0;
    baseline.measured = 0.0;
    baseline.lowerLimit = 0.0;
    baseline.upperLimit = 0.0;
    baseline.unit = "owned routes";
    baseline.verdict = RunVerdict::Ok;
    baseline.attributes = {
        {"baseline_reset_acknowledged", "true"},
        {"session_state", session->second},
        {"owned_count", owned->second},
        {"global_hardware_state", "not_readable"}};

    context.eventSink({std::chrono::system_clock::now(), node.id, "ISD_BASELINE",
        "Firmware подтвердил выполнение стартовой команды all-off baseline", RunVerdict::Ok,
        baseline.attributes});

    ProcedureResult result{RunVerdict::Ok,
        "Стартовая команда all-off baseline подтверждена; глобальное состояние аппаратно не читается",
        {}};
    result.measurements.push_back(std::move(baseline));
    return result;
}

ProcedureResult ytpStartAdapterOnly(const ScenarioNode& node, ProcedureContext& context)
{
    const auto record = responseValues(context.equipment.invoke(
        "ulk.parameter_source", "start_record", {{"run_id", context.runId}}));

    try {
        // YTP is an adapter/ROKT path.  The former ISD type=4/type=7 wrapper is
        // not part of the working route: type=4 is the heavy global all-off
        // service action and firmware case 7 has no confirmed useful action.
        context.equipment.invoke("ulk.parameter_source", "prepare_ytp_rokt", {});
        waitScaled(context, natural(node, "configure_settle_ms", 500));
        context.equipment.invoke("ulk.parameter_source", "start_prepared_ytp_rokt", {
            {"ytp_endpoint", argument(node, "ytp_endpoint", "1")}});
        waitScaled(context, natural(node, "stream_settle_ms", 1000));

        const auto response = responseValues(context.equipment.invoke(
            "ulk.parameter_source", "await_ytp_rokt", {
                {"ytp_endpoint", argument(node, "ytp_endpoint", "1")},
                {"stream_settle_ms", "0"},
                {"timeout_ms", std::to_string(natural(node, "timeout_ms", 3000))}}));

        context.state["ytp.protocol"] = response.count("protocol")
            ? response.at("protocol") : std::string("unknown");
        context.state["ytp.raw_path"] = record.count("path")
            ? record.at("path") : std::string();
        context.state["ytp.valid_word_count"] = response.count("valid_word_count")
            ? response.at("valid_word_count") : std::string("unknown");

        if (context.state.at("ytp.protocol") != "rokt_ytp68") {
            return {RunVerdict::Incomplete,
                "Адаптер не подтвердил рабочий поток ЯТП ROKT 68 байт", {}};
        }

        return {RunVerdict::Ok,
            "Запущен ЯТП через адаптер: ROKT 0A 02 00 01 00, принимаются кадры 68 байт; "
            "валидных слов в первом кадре " + context.state.at("ytp.valid_word_count")
            + "/32, raw сохраняется в " + context.state.at("ytp.raw_path"), {}};
    } catch (...) {
        try { context.equipment.invoke("ulk.parameter_source", "stop_stream", {}); }
        catch (...) {}
        try { context.equipment.invoke("ulk.parameter_source", "stop_record", {}); }
        catch (...) {}
        throw;
    }
}

ProcedureResult ytpSafeCleanupAdapterOnly(const ScenarioNode&, ProcedureContext& context)
{
    std::string failures;
    try { context.equipment.invoke("ulk.parameter_source", "stop_stream", {}); }
    catch (const std::exception& error) { failures = error.what(); }
    try { context.equipment.invoke("ulk.parameter_source", "stop_record", {}); }
    catch (const std::exception& error) {
        if (!failures.empty()) failures += "; ";
        failures += error.what();
    }

    if (!failures.empty()) {
        return {RunVerdict::Error,
            "Не все операции остановки ЯТП выполнены: " + failures, {}};
    }
    return {RunVerdict::Ok,
        "Поток и запись ЯТП остановлены; команды ИСД для ЯТП не используются", {}};
}

} // namespace

void registerProductionFinalUbsiProcedures(ScenarioEngine& engine)
{
    engine.registerProcedure("ubsi.isd_baseline", isdBaseline);
    // Final production override: preserve the already working adapter/ROKT YTP
    // path and remove only the obsolete ISD wrapper around it.
    engine.registerProcedure("ytp.start_stream", ytpStartAdapterOnly);
    engine.registerProcedure("ytp.safe_cleanup", ytpSafeCleanupAdapterOnly);
}

} // namespace orbita::stand
