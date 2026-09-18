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

void registerRoktUbsiProcedures(ScenarioEngine& engine);
void registerIsdSafeUbsiProcedures(ScenarioEngine& engine);
void registerProductionFinalUbsiProcedures(ScenarioEngine& engine);
void registerProductionYvpProcedure(ScenarioEngine& engine);

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

unsigned natural(const ScenarioNode& node, const std::string& key, unsigned fallback)
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

bool outputEnabled(const std::map<std::string, std::string>& state)
{
    const auto found = state.find("output_enabled");
    return found != state.end() && (found->second == "true" || found->second == "1");
}

bool restartSharedSupplyAdapter(ProcedureContext& context, unsigned timeoutMs)
{
    const auto started = std::chrono::steady_clock::now();
    do {
        if (context.stopRequested.load()) throw std::runtime_error("Остановлено оператором");
        const auto elapsed = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
        if (elapsed >= timeoutMs) return false;
        const unsigned attemptTimeout = std::max(1u, std::min(750u, timeoutMs - elapsed));
        try {
            context.equipment.invoke("ulk.parameter_source", "prepare_yalk_reference", {});
            const auto response = responseValues(context.equipment.invoke(
                "ulk.parameter_source", "start_prepared_yalk_reference", {
                    {"timeout_ms", std::to_string(attemptTimeout)}}));
            if (response.count("status") && response.at("status") == "ready") return true;
        } catch (const std::exception&) {
            // The adapter is powered through the UBSI from the same AKIP line.
            // During boot or recovery UDP commands may time out; retry the full
            // ROKT initialization, but never power-cycle AKIP from readiness.
        }
        const auto afterAttempt = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
        if (afterAttempt >= timeoutMs) return false;
        waitScaled(context, std::min(250u, timeoutMs - afterAttempt));
    } while (true);
}

ProcedureResult sharedSupplyReadiness(const ScenarioNode& node, ProcedureContext& context)
{
    const double voltage = number(node, "voltage_v", 27.0);
    const double currentLimit = number(node, "current_limit_a", 0.6);
    const unsigned timeoutMs = natural(node, "timeout_ms", 30000);
    const unsigned startupSettleMs = natural(
        node, "startup_settle_ms", natural(node, "off_settle_ms", 500));

    // HARD UBSI INVARIANT:
    // AKIP powers the UBSI and the UDP/RS-485 adapter through the same UBSI
    // supply path. Equipment preparation has already enabled AKIP before the
    // adapter check. Readiness must preserve that power while later tests remain.
    // It may arm known setpoints and recover an unexpectedly disabled output,
    // but it must never create an OFF -> ON power cycle of its own.
    auto state = responseValues(context.equipment.invoke("power.dc_supply", "read_state", {}));
    const bool wasEnabled = outputEnabled(state);

    context.equipment.invoke("power.dc_supply", "set_current_limit", {
        {"amperes", std::to_string(currentLimit)}});
    context.equipment.invoke("power.dc_supply", "set_voltage", {
        {"volts", std::to_string(voltage)}});

    const auto started = std::chrono::steady_clock::now();
    if (!wasEnabled) {
        // Fallback for a direct/headless invocation that bypassed the normal
        // equipment-preparation wizard. Enabling once is allowed; disabling is not.
        context.equipment.invoke("power.dc_supply", "output", {{"enabled", "true"}});
        waitScaled(context, startupSettleMs);
    }

    context.state["ubsi.shared_supply"] = "akip_ubsi_adapter";
    context.state["ubsi.akip_output_preserved"] = wasEnabled ? "true" : "enabled_without_cycle";

    const bool ready = restartSharedSupplyAdapter(context, timeoutMs);
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const double limitSeconds = timeoutMs / 1000.0;

    MeasurementResult measurement;
    measurement.parameterKey = "ubsi.ready_time";
    measurement.title = "Время подтверждения готовности тракта УБСИ";
    measurement.reference = 0.0;
    measurement.measured = ready ? seconds : limitSeconds + 0.001;
    measurement.lowerLimit = 0.0;
    measurement.upperLimit = limitSeconds;
    measurement.unit = "с";
    measurement.verdict = ready ? RunVerdict::Ok : RunVerdict::Fail;
    if (!ready) measurement.message = "Адаптер УБСИ не подтвердил свежий поток в нормативное время";
    measurement.attributes = {
        {"shared_supply", "akip_ubsi_adapter"},
        {"akip_output_was_enabled", wasEnabled ? "true" : "false"},
        {"akip_power_cycle_performed", "false"},
        {"nominal_voltage_v", std::to_string(voltage)},
        {"current_limit_a", std::to_string(currentLimit)}};

    ProcedureResult result;
    result.verdict = measurement.verdict;
    result.message = ready
        ? (wasEnabled
            ? "Питание АКИП сохранено; адаптер УБСИ подтвердил свежий поток после явной ROKT-инициализации"
            : "АКИП был выключен до процедуры и включён один раз без промежуточного power-cycle; адаптер подтвердил свежий поток")
        : "Адаптер УБСИ не подтвердил готовность при сохранённом питании АКИП";
    result.measurements.push_back(std::move(measurement));
    return result;
}

} // namespace

void registerUbsiProcedures(ScenarioEngine& engine)
{
    // Keep the validated YALK/current delivery stack, then layer the safe ISD
    // implementation and the final production lifecycle/YTP overrides on top.
    // Production YVP remains the single final ubsi.yvp implementation.
    registerRoktUbsiProcedures(engine);
    registerIsdSafeUbsiProcedures(engine);
    registerProductionFinalUbsiProcedures(engine);
    registerProductionYvpProcedure(engine);

    // Register last so no legacy layer can re-introduce the old readiness
    // power-cycle. Canonical physical rule: AKIP -> UBSI -> adapter, power stays
    // on while downstream tests remain.
    engine.registerProcedure("ubsi.readiness", sharedSupplyReadiness);
}

} // namespace orbita::stand
