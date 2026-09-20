#include "procedures/power_procedures.h"

#include "hardware/stand_hardware.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace tu::procedures {
namespace {

std::string argument(const ScenarioStep& step, const std::string& key,
                     std::string fallback = {})
{
    const auto found = step.arguments.find(key);
    return found == step.arguments.end() ? std::move(fallback) : found->second;
}

double number(const ScenarioStep& step, const std::string& key, double fallback = 0.0)
{
    const std::string text = argument(step, key);
    if (text.empty()) return fallback;
    std::size_t parsed = 0;
    const double value = std::stod(text, &parsed);
    if (parsed != text.size() || !std::isfinite(value))
        throw std::invalid_argument("Некорректный аргумент " + key);
    return value;
}

unsigned natural(const ScenarioStep& step, const std::string& key, unsigned fallback = 0)
{
    const std::string text = argument(step, key);
    if (text.empty()) return fallback;
    std::size_t parsed = 0;
    const auto value = std::stoul(text, &parsed, 0);
    if (parsed != text.size()) throw std::invalid_argument("Некорректный аргумент " + key);
    return static_cast<unsigned>(value);
}

std::vector<double> numbers(const ScenarioStep& step, const std::string& key)
{
    std::vector<double> result;
    std::stringstream stream(argument(step, key));
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (item.empty()) continue;
        std::size_t parsed = 0;
        const double value = std::stod(item, &parsed);
        if (parsed != item.size() || !std::isfinite(value))
            throw std::invalid_argument("Некорректный аргумент " + key);
        result.push_back(value);
    }
    return result;
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

void checkedWait(ProcedureContext& context, std::chrono::milliseconds duration)
{
    constexpr auto slice = std::chrono::milliseconds(50);
    auto remaining = duration;
    while (remaining.count() > 0) {
        context.checkpoint();
        const auto part = std::min(slice, remaining);
        std::this_thread::sleep_for(part);
        remaining -= part;
    }
}

void uncheckedWait(std::chrono::milliseconds duration)
{
    if (duration.count() > 0) std::this_thread::sleep_for(duration);
}

void publishPower(ProcedureContext& context, const ScenarioStep& step,
                  double setpoint, const hardware::PowerState& state,
                  unsigned elapsedSeconds = 0, unsigned durationSeconds = 0)
{
    if (!context.eventSink) return;
    context.eventSink({
        std::chrono::system_clock::now(), step.id, "SUPPLY", "Питание УБСИ",
        RunVerdict::NotRun,
        {
            {"setpoint_v", std::to_string(setpoint)},
            {"volts", std::to_string(state.measuredVoltageV)},
            {"amperes", std::to_string(state.measuredCurrentA)},
            {"elapsed_s", std::to_string(elapsedSeconds)},
            {"duration_s", std::to_string(durationSeconds)},
            {"output_enabled", state.outputEnabled ? "true" : "false"},
        }});
}

ProcedureResult readiness(const ScenarioStep& step, ProcedureContext& context,
                          const std::shared_ptr<hardware::StandHardware>& stand)
{
    auto& supply = stand->supply();
    auto& yalk = stand->yalk();
    const double voltage = number(step, "voltage_v", 27.0);
    const double currentLimit = number(step, "current_limit_a", 0.6);
    const unsigned timeoutMs = natural(step, "timeout_ms", 30000);
    const unsigned offSettleMs = natural(step, "off_settle_ms", 500);

    ProcedureResult result{RunVerdict::Ok,
        "УБСИ не выдал данные за нормативное время", {}};

    try {
        // Нормативное время отсчитывается именно от холодного включения.
        yalk.stop();
        supply.setOutput(false);
        checkedWait(context, std::chrono::milliseconds(offSettleMs));

        supply.setCurrentLimit(currentLimit);
        supply.setVoltage(voltage);
        const auto started = std::chrono::steady_clock::now();
        supply.setOutput(true);

        const bool ready = yalk.restartUntilReady(
            std::chrono::milliseconds(timeoutMs), [&context] { context.checkpoint(); });
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();

        const auto state = supply.readState();
        publishPower(context, step, voltage, state,
                     static_cast<unsigned>(seconds), timeoutMs / 1000);

        append(result, measurement(
            "ubsi.ready_time", "Время готовности", 0.0,
            ready ? seconds : timeoutMs / 1000.0 + 0.001,
            0.0, timeoutMs / 1000.0, "с"));
        if (ready)
            result.message = "УБСИ вышел на передачу данных после запуска ROKT ЯЛК";
        return result;
    } catch (...) {
        if (context.stopRequested.load()) stand->safeStop();
        throw;
    }
}

ProcedureResult supplyRange(const ScenarioStep& step, ProcedureContext& context,
                            const std::shared_ptr<hardware::StandHardware>& stand)
{
    auto& supply = stand->supply();
    auto& yalk = stand->yalk();

    const auto points = numbers(step, "voltage_points_v");
    if (points.empty()) throw std::invalid_argument("Для проверки питания нужны voltage_points_v");
    const auto survival = numbers(step, "survival_points_v");
    const auto durations = numbers(step, "survival_seconds");
    if (survival.size() != durations.size())
        throw std::invalid_argument("survival_points_v и survival_seconds должны совпадать");

    const double hardwareCurrentLimit = number(step, "supply_current_limit_a", 0.6);
    const double totalCurrentLimit = number(step, "maximum_total_current_a",
        number(step, "current_limit_a", 0.4));
    const double voltageTolerance = number(step, "voltage_tolerance_v", 0.5);
    const double restoreVoltage = number(step, "restore_voltage_v", 27.0);
    const unsigned settleMs = natural(step, "settle_ms", 500);
    const unsigned recoveryTimeoutMs = natural(step, "recovery_timeout_ms", 30000);

    ProcedureResult result{RunVerdict::Ok,
        "Проверены рабочий диапазон питания и общий ток потребления УБСИ", {}};

    const auto restoreChecked = [&] {
        supply.setVoltage(restoreVoltage);
        checkedWait(context, std::chrono::milliseconds(settleMs));
    };
    const auto restoreForSkip = [&] {
        try {
            supply.setVoltage(restoreVoltage);
            uncheckedWait(std::chrono::milliseconds(settleMs));
        } catch (...) {
            stand->safeStop();
        }
    };

    try {
        // Делаем power.range самодостаточным: даже если readiness был сервисно
        // пропущен, включение возможно только после новых проверенных уставок.
        supply.setCurrentLimit(hardwareCurrentLimit);
        supply.setVoltage(restoreVoltage);
        supply.setOutput(true);

        for (const double setpoint : points) {
            context.checkpoint();
            supply.setVoltage(setpoint);
            checkedWait(context, std::chrono::milliseconds(settleMs));
            const auto state = supply.readState();
            publishPower(context, step, setpoint, state);

            auto voltage = measurement(
                "ubsi.supply.voltage",
                "Фактическое питание при уставке " + std::to_string(setpoint) + " В",
                setpoint, state.measuredVoltageV,
                setpoint - voltageTolerance, setpoint + voltageTolerance, "В");
            voltage.attributes = {
                {"setpoint_v", std::to_string(setpoint)},
                {"supply_current_a", std::to_string(state.measuredCurrentA)}};
            append(result, std::move(voltage));

            auto current = measurement(
                "ubsi.supply.total_current",
                "Общий ток потребления УБСИ при " + std::to_string(setpoint) + " В",
                0.0, state.measuredCurrentA, 0.0, totalCurrentLimit, "А");
            current.attributes = {
                {"setpoint_v", std::to_string(setpoint)},
                {"measurement_scope", "whole_ubsi"},
                {"hardware_current_limit_a", std::to_string(hardwareCurrentLimit)}};
            append(result, std::move(current));

            const bool alive = yalk.waitNextReference(
                std::chrono::milliseconds(3000), [&context] { context.checkpoint(); });
            append(result, measurement(
                "ubsi.supply.data_alive",
                "Передача данных при " + std::to_string(setpoint) + " В",
                1.0, alive ? 1.0 : 0.0, 1.0, 1.0, "лог."));
        }

        for (std::size_t index = 0; index < survival.size(); ++index) {
            context.checkpoint();
            supply.setVoltage(survival[index]);
            const unsigned durationMs = static_cast<unsigned>(durations[index] * 1000.0);
            unsigned elapsed = 0;
            while (elapsed < durationMs) {
                const auto state = supply.readState();
                publishPower(context, step, survival[index], state,
                             elapsed / 1000, durationMs / 1000);
                const unsigned interval = std::min(1000u, durationMs - elapsed);
                checkedWait(context, std::chrono::milliseconds(interval));
                elapsed += interval;
            }

            const auto state = supply.readState();
            publishPower(context, step, survival[index], state,
                         durationMs / 1000, durationMs / 1000);
            auto held = measurement(
                "ubsi.supply.survival_voltage",
                "Фактическое напряжение выдержки " + std::to_string(survival[index]) + " В",
                survival[index], state.measuredVoltageV,
                survival[index] - voltageTolerance,
                survival[index] + voltageTolerance, "В");
            held.attributes = {{"duration_s", std::to_string(durations[index])}};
            append(result, std::move(held));

            restoreChecked();
            const bool recovered = yalk.restartUntilReady(
                std::chrono::milliseconds(recoveryTimeoutMs),
                [&context] { context.checkpoint(); });
            append(result, measurement(
                "ubsi.supply.survived",
                "Работоспособность после предельного напряжения и возврата к 27 В",
                1.0, recovered ? 1.0 : 0.0, 1.0, 1.0, "лог."));
        }

        restoreChecked();
        return result;
    } catch (...) {
        if (context.skipRequested.load() && !context.stopRequested.load())
            restoreForSkip();
        else
            stand->safeStop();
        throw;
    }
}

ProcedureResult powerOff(const ScenarioStep&, ProcedureContext& context,
                         const std::shared_ptr<hardware::StandHardware>& stand)
{
    context.checkpoint();
    stand->yalk().stop();
    stand->supply().setOutput(false);
    const auto state = stand->supply().readState();

    ProcedureResult result{RunVerdict::Ok, "Выход АКИП выключен после испытания", {}};
    auto value = measurement(
        "ubsi.supply.safe_off", "Безопасное отключение АКИП",
        0.0, state.outputEnabled ? 1.0 : 0.0, 0.0, 0.0, "лог.");
    value.attributes = {
        {"supply_voltage_v", std::to_string(state.measuredVoltageV)},
        {"supply_current_a", std::to_string(state.measuredCurrentA)},
        {"output_enabled", state.outputEnabled ? "1" : "0"}};
    append(result, std::move(value));
    return result;
}

ProcedureResult unavailable(const ScenarioStep& step, ProcedureContext&)
{
    return {RunVerdict::Incomplete,
        "Процедура " + step.procedure
            + " ещё не перенесена в clean-ветку; воздействие не выполнялось", {}};
}

} // namespace

void registerPowerProcedures(ScenarioEngine& engine,
                             std::shared_ptr<hardware::StandHardware> hardware)
{
    if (!hardware) throw std::invalid_argument("StandHardware is required");

    engine.registerProcedure("power.readiness",
        [hardware](const ScenarioStep& step, ProcedureContext& context) {
            return readiness(step, context, hardware);
        });
    engine.registerProcedure("power.range",
        [hardware](const ScenarioStep& step, ProcedureContext& context) {
            return supplyRange(step, context, hardware);
        });
    engine.registerProcedure("power.off",
        [hardware](const ScenarioStep& step, ProcedureContext& context) {
            return powerOff(step, context, hardware);
        });
}

void registerUnavailableProcedures(ScenarioEngine& engine)
{
    for (const char* id : {
        "yalk.start", "yalk.calibration", "yalk.initial", "yalk.channels",
        "yalk.overload", "yalk.reference", "yalk.finish",
        "ytp.start", "ytp.calibration", "ytp.channels", "ytp.finish",
        "yvp.run"}) {
        engine.registerProcedure(id, unavailable);
    }
}

} // namespace tu::procedures
