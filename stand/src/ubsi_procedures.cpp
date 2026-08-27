#include "orbita_stand/ubsi_procedures.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace orbita::stand {
namespace {

std::map<std::string, std::string> responseValues(const std::string& response)
{
    std::map<std::string, std::string> result;
    std::stringstream stream(response);
    std::string line;
    while (std::getline(stream, line)) {
        const auto equal = line.find('=');
        if (equal != std::string::npos && equal) result[line.substr(0, equal)] = line.substr(equal + 1);
    }
    return result;
}

std::string argument(const ScenarioNode& node, const std::string& key, std::string fallback = {})
{
    const auto iterator = node.arguments.find(key);
    return iterator == node.arguments.end() ? std::move(fallback) : iterator->second;
}

double number(const ScenarioNode& node, const std::string& key, double fallback = 0.0)
{
    const auto value = argument(node, key);
    if (value.empty()) return fallback;
    std::size_t parsed = 0;
    const double result = std::stod(value, &parsed);
    if (parsed != value.size() || !std::isfinite(result)) throw std::invalid_argument("Некорректный аргумент " + key);
    return result;
}

unsigned natural(const ScenarioNode& node, const std::string& key, unsigned fallback = 0)
{
    const auto value = argument(node, key);
    if (value.empty()) return fallback;
    std::size_t parsed = 0;
    const unsigned long result = std::stoul(value, &parsed, 0);
    if (parsed != value.size()) throw std::invalid_argument("Некорректный аргумент " + key);
    return static_cast<unsigned>(result);
}

std::vector<double> numbers(const ScenarioNode& node, const std::string& key)
{
    std::vector<double> result;
    std::stringstream stream(argument(node, key));
    std::string item;
    while (std::getline(stream, item, ',')) if (!item.empty()) result.push_back(std::stod(item));
    return result;
}

double responseNumber(const std::string& response, const std::string& key)
{
    const auto values = responseValues(response);
    const auto iterator = values.find(key);
    if (iterator == values.end()) throw std::runtime_error("Прибор не вернул поле " + key);
    return std::stod(iterator->second);
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

void wait(ProcedureContext& context, unsigned milliseconds)
{
    constexpr unsigned slice = 50;
    for (unsigned elapsed = 0; elapsed < milliseconds; elapsed += slice) {
        if (context.stopRequested.load()) throw std::runtime_error("Остановлено оператором");
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(slice, milliseconds - elapsed)));
    }
}

double readReferenceVoltage(ProcedureContext& context)
{
    return responseNumber(context.equipment.invoke(
        "measure.reference_voltage", "read_voltage", {}), "volts");
}

double readYalkRaw(
    ProcedureContext& context, const std::string& parameterKey, unsigned offset,
    unsigned samples, unsigned mask = 0xFFFF)
{
    return responseNumber(context.equipment.invoke("ubsi.parameter_source", "read_yalk", {
        {"parameter_key", parameterKey}, {"offset", std::to_string(offset)},
        {"sample_count", std::to_string(samples)},
        {"mask", std::to_string(mask)}}), "raw");
}

ProcedureResult readiness(const ScenarioNode& node, ProcedureContext& context)
{
    ProcedureResult result{RunVerdict::Fail, "УБСИ не выдал данные за нормативное время", {}};
    const double voltage = number(node, "voltage_v", 27.0);
    const unsigned timeoutMs = natural(node, "timeout_ms", 30000);
    context.equipment.invoke("power.dc_supply", "set_voltage", {{"volts", std::to_string(voltage)}});
    context.equipment.invoke("power.dc_supply", "output", {{"enabled", "true"}});
    const auto started = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - started).count() <= timeoutMs) {
        if (context.stopRequested.load()) throw std::runtime_error("Остановлено оператором");
        const auto status = responseValues(context.equipment.invoke(
            "ubsi.parameter_source", "alive", {}));
        if (status.count("status") && status.at("status") == "ready") {
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            append(result, measurement("ubsi.ready_time", "Время готовности", 0.0, seconds,
                                       0.0, timeoutMs / 1000.0, "с"));
            result.message = "УБСИ вышел на передачу данных";
            return result;
        }
        wait(context, std::min(250u, timeoutMs));
    }
    append(result, measurement("ubsi.ready_time", "Время готовности", 0.0,
                               timeoutMs / 1000.0 + 0.001, 0.0, timeoutMs / 1000.0, "с"));
    return result;
}

ProcedureResult supplyRange(const ScenarioNode& node, ProcedureContext& context)
{
    ProcedureResult result{RunVerdict::Ok, "Проверены питание и ток потребления", {}};
    const auto points = numbers(node, "voltage_points_v");
    if (points.empty()) throw std::invalid_argument("Для проверки питания нужны voltage_points_v");
    const unsigned settleMs = natural(node, "settle_ms", 500);
    const double currentLimit = number(node, "current_limit_a", 0.4);
    for (const double voltage : points) {
        context.equipment.invoke("power.dc_supply", "set_voltage", {{"volts", std::to_string(voltage)}});
        wait(context, settleMs);
        const double current = responseNumber(context.equipment.invoke(
            "measure.dc_current", "read_current", {}), "amperes");
        append(result, measurement("ubsi.supply_current", "Ток при " + std::to_string(voltage) + " В",
                                   0.0, current, 0.0, currentLimit, "А"));
        const auto alive = responseValues(context.equipment.invoke("ubsi.parameter_source", "alive", {}));
        if (!alive.count("status") || alive.at("status") != "ready") {
            MeasurementResult data = measurement("ubsi.data_alive", "Передача данных", 1, 0, 1, 1, "лог.");
            append(result, std::move(data));
        }
    }
    const auto survival = numbers(node, "survival_points_v");
    const auto durations = numbers(node, "survival_seconds");
    if (survival.size() != durations.size()) throw std::invalid_argument("survival_points_v и survival_seconds должны совпадать");
    for (std::size_t index = 0; index < survival.size(); ++index) {
        context.equipment.invoke("power.dc_supply", "set_voltage", {{"volts", std::to_string(survival[index])}});
        wait(context, static_cast<unsigned>(durations[index] * 1000.0));
        const auto alive = responseValues(context.equipment.invoke("ubsi.parameter_source", "alive", {}));
        append(result, measurement("ubsi.survival", "Работа при " + std::to_string(survival[index]) + " В",
            1, alive.count("status") && alive.at("status") == "ready" ? 1 : 0, 1, 1, "лог."));
    }
    return result;
}

ProcedureResult sensorSupply(const ScenarioNode& node, ProcedureContext& context)
{
    ProcedureResult result{RunVerdict::Ok, "Проверено питание датчиков и восстановление защиты", {}};
    const unsigned type = natural(node, "switch_type", 2);
    const std::string load350 = argument(node, "load_350_route", "sensor_load_350ma");
    const std::string load450 = argument(node, "load_450_route", "sensor_load_450ma");
    const unsigned settle = natural(node, "settle_ms", 500);
    const double lower = number(node, "voltage_min_v", 6.0);
    const double upper = number(node, "voltage_max_v", 6.4);
    const double tripMaximum = number(node, "trip_max_v", 1.0);
    append(result, measurement("ubsi.sensor_supply", "Питание датчиков без нагрузки", 6.2,
                               readReferenceVoltage(context), lower, upper, "В"));
    context.equipment.invoke("stand.switch_matrix", "switch", {
        {"type", std::to_string(type)}, {"route", load350}, {"enabled", "true"}});
    wait(context, settle);
    append(result, measurement("ubsi.sensor_supply_350ma", "Питание при 350 мА", 6.2,
                               readReferenceVoltage(context), lower, upper, "В"));
    context.equipment.invoke("stand.switch_matrix", "switch", {
        {"type", std::to_string(type)}, {"route", load350}, {"enabled", "false"}});
    context.equipment.invoke("stand.switch_matrix", "switch", {
        {"type", std::to_string(type)}, {"route", load450}, {"enabled", "true"}});
    wait(context, settle);
    append(result, measurement("ubsi.sensor_supply_trip", "Срабатывание защиты при 450 мА", 0.0,
                               readReferenceVoltage(context), 0.0, tripMaximum, "В"));
    context.equipment.invoke("stand.switch_matrix", "switch", {
        {"type", std::to_string(type)}, {"route", load450}, {"enabled", "false"}});
    wait(context, settle);
    append(result, measurement("ubsi.sensor_supply_recovery", "Восстановление питания датчиков", 6.2,
                               readReferenceVoltage(context), lower, upper, "В"));
    return result;
}

ProcedureResult yalkAnalog(const ScenarioNode& node, ProcedureContext& context)
{
    ProcedureResult result{RunVerdict::Ok, "Проверены аналоговые каналы ЯЛК", {}};
    const unsigned channelCount = natural(node, "channel_count", 60);
    const std::string parameterGroup = argument(node, "parameter_group", "yalk_analog");
    const std::string isdRoute = argument(node, "isd_analog_route", "yalk_analog");
    const unsigned samples = natural(node, "sample_count", 16);
    const unsigned settle = natural(node, "settle_ms", 150);
    const double fullScale = number(node, "full_scale_v", 6.2);
    const double tolerance = fullScale * number(node, "tolerance_percent_fs", 0.5) / 100.0;
    const auto pointVolts = numbers(node, "point_volts");
    const auto pointCodes = numbers(node, "isd_codes");
    if (!channelCount || pointVolts.empty() || pointVolts.size() != pointCodes.size()) {
        throw std::invalid_argument("Не заполнена методика ЯЛК: point_volts/isd_codes");
    }
    double zeroRaw = number(node, "calibration_zero_raw", 0.0);
    double fullRaw = number(node, "calibration_full_raw", 0.0);
    const std::string zeroParameter = argument(node, "calibration_zero_parameter");
    const std::string fullParameter = argument(node, "calibration_full_parameter");
    if (!zeroParameter.empty() && !fullParameter.empty()) {
        zeroRaw = readYalkRaw(context, zeroParameter, 0, samples);
        fullRaw = readYalkRaw(context, fullParameter, 0, samples);
    }
    if (!(fullRaw > zeroRaw)) throw std::invalid_argument("Не настроены калибровочные коды ЯЛК");
    for (unsigned channel = 0; channel < channelCount; ++channel) {
        for (std::size_t point = 0; point < pointVolts.size(); ++point) {
            context.equipment.invoke("stand.switch_matrix", "analog", {
                {"route", isdRoute}, {"offset", std::to_string(channel)},
                {"value", std::to_string(static_cast<unsigned>(pointCodes[point]))}, {"enabled", "true"}});
            wait(context, settle);
            const double reference = readReferenceVoltage(context);
            const double raw = readYalkRaw(context, parameterGroup, channel, samples);
            const double measured = (raw - zeroRaw) * fullScale / (fullRaw - zeroRaw);
            append(result, measurement("ubsi.yalk." + std::to_string(channel + 1),
                "ЯЛК канал " + std::to_string(channel + 1), reference, measured,
                reference - tolerance, reference + tolerance, "В"));
            context.equipment.invoke("stand.switch_matrix", "analog", {
                {"route", isdRoute}, {"offset", std::to_string(channel)}, {"value", "0"}, {"enabled", "false"}});
        }
    }
    return result;
}

ProcedureResult yalkContacts(const ScenarioNode& node, ProcedureContext& context)
{
    ProcedureResult result{RunVerdict::Ok, "Проверены контактные каналы", {}};
    const unsigned count = natural(node, "channel_count", 30);
    const std::string switchRoute = argument(node, "isd_switch_route", "yalk_contacts");
    const std::string parameterGroup = argument(node, "parameter_group", "yalk_contacts");
    const unsigned mask = natural(node, "mask", 1);
    for (unsigned channel = 0; channel < count; ++channel) {
        for (const bool state : {false, true}) {
            context.equipment.invoke("stand.switch_matrix", "switch", {
                {"route", switchRoute}, {"offset", std::to_string(channel)}, {"enabled", state ? "true" : "false"}});
            wait(context, natural(node, "settle_ms", 100));
            const double raw = readYalkRaw(context, parameterGroup, channel, natural(node, "sample_count", 4), mask);
            append(result, measurement("ubsi.yalk.contact." + std::to_string(channel + 1),
                "Контактный канал " + std::to_string(channel + 1), state ? 1 : 0,
                raw > 0.5 ? 1 : 0, state ? 1 : 0, state ? 1 : 0, "лог."));
        }
    }
    return result;
}

ProcedureResult yalkFaults(const ScenarioNode& node, ProcedureContext& context)
{
    ProcedureResult result{RunVerdict::Ok, "Проверены обрыв и перегрузка входов", {}};
    const std::string openRoute = argument(node, "open_route", "yalk_open");
    const std::string positiveRoute = argument(node, "positive_overload_route", "yalk_overload_positive");
    const std::string negativeRoute = argument(node, "negative_overload_route", "yalk_overload_negative");
    for (const auto& item : std::vector<std::pair<std::string, std::string>>{
             {openRoute, "Обрыв"}, {positiveRoute, "+12 В"}, {negativeRoute, "-12 В"}}) {
        context.equipment.invoke("stand.switch_matrix", "switch", {
            {"route", item.first}, {"enabled", "true"}});
        wait(context, natural(node, "settle_ms", 150));
        const auto status = responseValues(context.equipment.invoke("ubsi.parameter_source", "alive", {}));
        append(result, measurement("ubsi.yalk.fault", item.second + ": остальные каналы работают", 1,
            status.count("status") && status.at("status") == "ready" ? 1 : 0, 1, 1, "лог."));
        context.equipment.invoke("stand.switch_matrix", "switch", {
            {"route", item.first}, {"enabled", "false"}});
    }
    return result;
}

ProcedureResult referenceVoltage(const ScenarioNode& node, ProcedureContext& context)
{
    ProcedureResult result{RunVerdict::Ok, "Проверено эталонное напряжение", {}};
    const double nominal = number(node, "nominal_v", 6.2);
    const double tolerance = number(node, "tolerance_v", 0.03);
    append(result, measurement("ubsi.reference_6v2", "Эталонное напряжение", nominal,
        readReferenceVoltage(context), nominal - tolerance, nominal + tolerance, "В"));
    return result;
}

ProcedureResult ytp(const ScenarioNode& node, ProcedureContext& context)
{
    ProcedureResult result{RunVerdict::Ok, "Проверены температурные каналы ЯТП", {}};
    const unsigned count = natural(node, "channel_count", 30);
    const std::string parameterGroup = argument(node, "parameter_group", "ytp_temperature");
    const auto points = numbers(node, "resistance_points_ohm");
    const double zeroRaw = number(node, "calibration_zero_raw");
    const double fullRaw = number(node, "calibration_full_raw");
    const double fullScale = number(node, "full_scale_ohm", 240.0);
    const double tolerance = fullScale * number(node, "tolerance_percent_fs", 0.5) / 100.0;
    if (points.empty() || !(fullRaw > zeroRaw)) throw std::invalid_argument("Не заполнена карта/калибровка ЯТП");
    for (const double resistance : points) {
        context.equipment.invoke("signal.resistance", "set_resistance", {{"ohms", std::to_string(resistance)}});
        wait(context, natural(node, "settle_ms", 1500));
        for (unsigned channel = 0; channel < count; ++channel) {
            const double raw = readYalkRaw(context, parameterGroup, channel, natural(node, "sample_count", 16));
            const double measured = (raw - zeroRaw) * fullScale / (fullRaw - zeroRaw);
            append(result, measurement("ubsi.ytp." + std::to_string(channel + 1),
                "ЯТП канал " + std::to_string(channel + 1), resistance, measured,
                resistance - tolerance, resistance + tolerance, "Ом"));
        }
    }
    return result;
}

ProcedureResult yvp(const ScenarioNode& node, ProcedureContext& context)
{
    ProcedureResult result{RunVerdict::Ok, "Проверены каналы ЯВП, АЧХ и усиление", {}};
    const unsigned channels = natural(node, "channel_count", 8);
    const auto frequencies = numbers(node, "frequencies_hz");
    const auto gains = numbers(node, "gains_mv_per_pcl");
    if (frequencies.empty() || gains.empty()) throw std::invalid_argument("Не заданы частоты и коэффициенты ЯВП");
    const double amplitude = number(node, "amplitude_vpp", 0.3875);
    for (unsigned channel = 1; channel <= channels; ++channel) {
        double referenceVpp = 0.0;
        for (const double frequency : frequencies) {
            context.equipment.invoke("signal.generator", "set_sine", {
                {"channel", "1"}, {"frequency_hz", std::to_string(frequency)},
                {"amplitude_vpp", std::to_string(amplitude)}, {"offset_v", "0"}});
            context.equipment.invoke("signal.generator", "output", {{"channel", "1"}, {"enabled", "true"}});
            wait(context, natural(node, "settle_ms", 200));
            const auto scope = responseValues(context.equipment.invoke("measure.waveform", "capture", {
                {"channel", std::to_string(natural(node, "scope_channel", 1))}}));
            const double vpp = std::stod(scope.at("maximum_v")) - std::stod(scope.at("minimum_v"));
            if (std::abs(frequency - number(node, "reference_frequency_hz", 1000.0)) < 0.001) referenceVpp = vpp;
            const double reference = referenceVpp > 0.0 ? referenceVpp : amplitude;
            const double percent = std::abs(frequency - 2.0) < 0.001 || std::abs(frequency - 2000.0) < 0.001
                ? number(node, "edge_tolerance_percent", 10.0) : number(node, "middle_tolerance_percent", 5.0);
            append(result, measurement("ubsi.yvp." + std::to_string(channel),
                "ЯВП " + std::to_string(channel) + ", " + std::to_string(frequency) + " Гц",
                reference, vpp, reference * (1.0 - percent / 100.0), reference * (1.0 + percent / 100.0), "В пик-пик"));
        }
        for (std::size_t gainIndex = 0; gainIndex < gains.size(); ++gainIndex) {
            const double gain = gains[gainIndex];
            // Выбор усиления выполняется внешней кроссировкой ИСД; профиль задаёт
            // базовый канал и один шаг на значение ряда.
            context.equipment.invoke("stand.switch_matrix", "switch", {
                {"route", argument(node, "gain_route", "yvp_gain")},
                 {"offset", std::to_string((channel - 1) * gains.size() + gainIndex)}, {"enabled", "true"}});
            append(result, measurement("ubsi.yvp.gain." + std::to_string(channel),
                "ЯВП " + std::to_string(channel) + ": коэффициент " + std::to_string(gain),
                1, 1, 1, 1, "лог."));
            context.equipment.invoke("stand.switch_matrix", "switch", {
                {"route", argument(node, "gain_route", "yvp_gain")},
                 {"offset", std::to_string((channel - 1) * gains.size() + gainIndex)}, {"enabled", "false"}});
        }
    }
    context.equipment.invoke("signal.generator", "output", {{"channel", "1"}, {"enabled", "false"}});
    return result;
}

} // namespace

void registerUbsiProcedures(ScenarioEngine& engine)
{
    engine.registerProcedure("ubsi.readiness", readiness);
    engine.registerProcedure("ubsi.supply_range", supplyRange);
    engine.registerProcedure("ubsi.sensor_supply", sensorSupply);
    engine.registerProcedure("ubsi.yalk_analog", yalkAnalog);
    engine.registerProcedure("ubsi.yalk_contacts", yalkContacts);
    engine.registerProcedure("ubsi.yalk_faults", yalkFaults);
    engine.registerProcedure("ubsi.reference_voltage", referenceVoltage);
    engine.registerProcedure("ubsi.ytp", ytp);
    engine.registerProcedure("ubsi.yvp", yvp);
}

} // namespace orbita::stand
