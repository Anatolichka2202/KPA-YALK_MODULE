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

unsigned responseUnsigned(const std::string& response, const std::string& key)
{
    const auto values = responseValues(response);
    const auto iterator = values.find(key);
    if (iterator == values.end()) throw std::runtime_error("Адаптер не вернул поле " + key);
    return static_cast<unsigned>(std::stoul(iterator->second));
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

double readReferenceAcVoltage(ProcedureContext& context)
{
    return responseNumber(context.equipment.invoke(
        "measure.reference_ac_voltage", "read_ac_voltage", {}), "volts");
}

double readReferenceFrequency(ProcedureContext& context)
{
    return responseNumber(context.equipment.invoke(
        "measure.reference_frequency", "read_frequency", {}), "hertz");
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

bool bindingsReady(ProcedureContext& context, const std::string& group,
                   unsigned count, bool requireStimulus = false)
{
    for (unsigned channel = 0; channel < count; ++channel) {
        const auto binding = resolveLogicalBinding(context, group, channel);
        if (!binding.confirmed || binding.locator.empty()
            || (requireStimulus && binding.stimulusRoute.empty())) return false;
    }
    return true;
}

double readLogicalParameter(
    ProcedureContext& context, const std::string& parameterKey, unsigned offset,
    unsigned samples)
{
    const auto binding = resolveLogicalBinding(context, parameterKey, offset);
    if (!context.equipment.hasCapability(binding.source)) {
        throw std::runtime_error("Для " + parameterKey + " недоступен источник " + binding.source);
    }
    if (binding.source == "ulk.parameter_source") {
        const double raw = responseNumber(context.equipment.invoke(binding.source, "read_channel", {
            {binding.locatorType == "ulk_address" ? "ulk_address" : "word_index", binding.locator},
            {"locator_type", binding.locatorType},
            {"parameter_group", parameterKey},
            {"sample_count", std::to_string(samples)},
            {"mask", std::to_string(binding.mask)}}), "raw_mean");
        return static_cast<unsigned>(std::llround(raw)) & binding.mask
            ? static_cast<double>((static_cast<unsigned>(std::llround(raw)) & binding.mask)
                                  >> binding.shift)
            : 0.0;
    }
    if (binding.source == "orbita.parameter_source") {
        return responseNumber(context.equipment.invoke(binding.source, "read", {
            {"address", binding.locator},
            {"sample_count", std::to_string(samples)}}), "value");
    }
    throw std::runtime_error("Каталог вернул неподдерживаемый источник " + binding.source);
}

ProcedureResult bindingCheck(const ScenarioNode& node, ProcedureContext& context)
{
    const std::string group = argument(node, "parameter_group");
    const unsigned count = natural(node, "channel_count");
    if (group.empty() || !count) throw std::invalid_argument("Не задана группа/число каналов каталога");
    bool confirmed = true;
    const bool requireStimulus = argument(node, "require_stimulus_route") == "true";
    std::string source;
    for (unsigned channel = 0; channel < count; ++channel) {
        const auto binding = resolveLogicalBinding(context, group, channel);
        if (source.empty()) source = binding.source;
        if (binding.source != source) {
            throw std::runtime_error("Группа " + group + " смешивает разные источники");
        }
        confirmed = confirmed && binding.confirmed && !binding.locator.empty()
            && (!requireStimulus || !binding.stimulusRoute.empty());
    }
    return confirmed
        ? ProcedureResult{RunVerdict::Ok,
            "БД: " + group + " → " + source + ", каналов " + std::to_string(count), {}}
        : ProcedureResult{RunVerdict::Incomplete,
            "БД содержит " + group + " → " + source
                + ", но стендовая привязка ещё не подтверждена живыми данными", {}};
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
            "ulk.parameter_source", "alive", {}));
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
    context.equipment.invoke("power.dc_supply", "set_current_limit", {
        {"amperes", std::to_string(number(node, "supply_current_limit_a", 0.6))}});
    for (const double voltage : points) {
        context.equipment.invoke("power.dc_supply", "set_voltage", {{"volts", std::to_string(voltage)}});
        wait(context, settleMs);
        const double current = responseNumber(context.equipment.invoke(
            "power.dc_supply", "read_state", {}), "amperes");
        append(result, measurement("ubsi.supply_current", "Ток при " + std::to_string(voltage) + " В",
                                   0.0, current, 0.0, currentLimit, "А"));
        const auto alive = responseValues(context.equipment.invoke("ulk.parameter_source", "alive", {}));
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
        const auto alive = responseValues(context.equipment.invoke("ulk.parameter_source", "alive", {}));
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
    const unsigned samples = natural(node, "sample_count", 16);
    const unsigned settle = natural(node, "settle_ms", 150);
    const double fullScale = number(node, "full_scale_v", 6.2);
    const double tolerance = fullScale * number(node, "tolerance_percent_fs", 0.5) / 100.0;
    const auto pointVolts = numbers(node, "point_volts");
    const auto pointCodes = numbers(node, "isd_codes");
    if (!channelCount || pointVolts.empty() || pointVolts.size() != pointCodes.size()) {
        throw std::invalid_argument("Не заполнена методика ЯЛК: point_volts/isd_codes");
    }
    const std::string zeroParameter = argument(node, "calibration_zero_parameter");
    const std::string fullParameter = argument(node, "calibration_full_parameter");
    if (!bindingsReady(context, parameterGroup, channelCount, true)
        || (!zeroParameter.empty() && !bindingsReady(context, zeroParameter, 1))
        || (!fullParameter.empty() && !bindingsReady(context, fullParameter, 1))) {
        return {RunVerdict::Incomplete,
            "Адреса УЛК/маршруты ЯЛК не подтверждены; воздействия не выполнялись", {}};
    }
    double zeroRaw = number(node, "calibration_zero_raw", 0.0);
    double fullRaw = number(node, "calibration_full_raw", 0.0);
    if (!zeroParameter.empty() && !fullParameter.empty()) {
        zeroRaw = readLogicalParameter(context, zeroParameter, 0, samples);
        fullRaw = readLogicalParameter(context, fullParameter, 0, samples);
    }
    if (!(fullRaw > zeroRaw)) throw std::invalid_argument("Не настроены калибровочные коды ЯЛК");
    for (unsigned channel = 0; channel < channelCount; ++channel) {
        const auto binding = resolveLogicalBinding(context, parameterGroup, channel);
        if (binding.stimulusRoute.empty()) {
            throw std::runtime_error("В БД не задан маршрут воздействия для "
                + parameterGroup + "/" + std::to_string(channel + 1));
        }
        for (std::size_t point = 0; point < pointVolts.size(); ++point) {
            context.equipment.invoke("stand.switch_matrix", "analog", {
                {"route", binding.stimulusRoute}, {"offset", std::to_string(binding.stimulusOffset)},
                {"value", std::to_string(static_cast<unsigned>(pointCodes[point]))}, {"enabled", "true"}});
            wait(context, settle);
            const double reference = readReferenceVoltage(context);
            const double raw = readLogicalParameter(context, parameterGroup, channel, samples);
            const double measured = (raw - zeroRaw) * fullScale / (fullRaw - zeroRaw);
            append(result, measurement("ubsi.yalk." + std::to_string(channel + 1),
                "ЯЛК канал " + std::to_string(channel + 1), reference, measured,
                reference - tolerance, reference + tolerance, "В"));
            context.equipment.invoke("stand.switch_matrix", "analog", {
                {"route", binding.stimulusRoute}, {"offset", std::to_string(binding.stimulusOffset)}, {"value", "0"}, {"enabled", "false"}});
        }
    }
    return result;
}

ProcedureResult yalkContacts(const ScenarioNode& node, ProcedureContext& context)
{
    ProcedureResult result{RunVerdict::Ok, "Проверены контактные каналы", {}};
    const unsigned count = natural(node, "channel_count", 30);
    const std::string parameterGroup = argument(node, "parameter_group", "yalk_contacts");
    if (!bindingsReady(context, parameterGroup, count, true)) {
        return {RunVerdict::Incomplete,
            "Адреса УЛК/маршруты контактных каналов не подтверждены; воздействия не выполнялись", {}};
    }
    for (unsigned channel = 0; channel < count; ++channel) {
        const auto binding = resolveLogicalBinding(context, parameterGroup, channel);
        if (binding.stimulusRoute.empty()) {
            throw std::runtime_error("В БД не задан маршрут воздействия для "
                + parameterGroup + "/" + std::to_string(channel + 1));
        }
        for (const bool state : {false, true}) {
            context.equipment.invoke("stand.switch_matrix", "switch", {
                {"route", binding.stimulusRoute}, {"offset", std::to_string(binding.stimulusOffset)}, {"enabled", state ? "true" : "false"}});
            wait(context, natural(node, "settle_ms", 100));
            const double raw = readLogicalParameter(
                context, parameterGroup, channel, natural(node, "sample_count", 4));
            append(result, measurement("ubsi.yalk.contact." + std::to_string(channel + 1),
                "Контактный канал " + std::to_string(channel + 1), state ? 1 : 0,
                raw > 0.5 ? 1 : 0, state ? 1 : 0, state ? 1 : 0, "лог."));
        }
    }
    return result;
}

ProcedureResult yalkFaults(const ScenarioNode& node, ProcedureContext& context)
{
    if (argument(node, "diagnostic_mapping_confirmed", "false") != "true") {
        return {RunVerdict::Incomplete,
            "Адреса УЛК найдены, но алгоритм сравнения сохранённых кодов при ±12 В "
            "ещё не подтверждён на подключённом УБСИ; опасное воздействие не выполнялось", {}};
    }
    ProcedureResult result{RunVerdict::Ok, "Проверены обрыв и перегрузка входов", {}};
    const std::string openRoute = argument(node, "open_route", "yalk_open");
    const std::string positiveRoute = argument(node, "positive_overload_route", "yalk_overload_positive");
    const std::string negativeRoute = argument(node, "negative_overload_route", "yalk_overload_negative");
    for (const auto& item : std::vector<std::pair<std::string, std::string>>{
             {openRoute, "Обрыв"}, {positiveRoute, "+12 В"}, {negativeRoute, "-12 В"}}) {
        context.equipment.invoke("stand.switch_matrix", "switch", {
            {"route", item.first}, {"enabled", "true"}});
        wait(context, natural(node, "settle_ms", 150));
        const auto status = responseValues(context.equipment.invoke("ulk.parameter_source", "alive", {}));
        append(result, measurement("ubsi.yalk.fault", item.second + ": остальные каналы работают", 1,
            status.count("status") && status.at("status") == "ready" ? 1 : 0, 1, 1, "лог."));
        context.equipment.invoke("stand.switch_matrix", "switch", {
            {"route", item.first}, {"enabled", "false"}});
    }
    return result;
}

struct UlkChannelValue {
    double raw = 0.0;
    double code = 0.0;
    bool signal = false;
    unsigned firstSequence = 0;
    unsigned lastSequence = 0;
};

unsigned ulkLastSequence(ProcedureContext& context)
{
    return responseUnsigned(context.equipment.invoke(
        "ulk.parameter_source", "stats", {}), "last_sequence");
}

UlkChannelValue readUlkChannel(
    ProcedureContext& context, unsigned address, unsigned samples, unsigned afterSequence)
{
    const auto response = context.equipment.invoke("ulk.parameter_source", "read_channel", {
        {"ulk_address", std::to_string(address)},
        {"sample_count", std::to_string(samples)},
        {"after_sequence", std::to_string(afterSequence)},
        {"timeout_ms", "3000"}});
    return {
        responseNumber(response, "raw_mean"),
        responseNumber(response, "analog_code_mean"),
        responseBool(response, "signal"),
        responseUnsigned(response, "first_sequence"),
        responseUnsigned(response, "last_sequence")};
}

double stateNumber(const ProcedureContext& context, const std::string& key)
{
    const auto found = context.state.find(key);
    if (found == context.state.end()) throw std::runtime_error("Нет состояния сценария ЯЛК: " + key);
    return std::stod(found->second);
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
        context.equipment.invoke(
            "stand.switch_matrix", "yalk_set_voltage", voltageArguments);
    } else {
        context.equipment.invoke(
            "stand.switch_matrix", "yalk_output_off", arguments);
    }
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

ProcedureResult yalkStartStream(const ScenarioNode& node, ProcedureContext& context)
{
    context.equipment.invoke("stand.switch_matrix", "yalk_prepare", {});
    context.equipment.invoke("ulk.parameter_source", "start_record", {{"run_id", context.runId}});
    context.equipment.invoke(
        "ulk.parameter_source", "prepare_yalk_reference", {});
    wait(context, natural(node, "configure_settle_ms", 500));
    context.equipment.invoke("stand.switch_matrix", "yalk_prepare", {});
    const auto response = responseValues(context.equipment.invoke(
        "ulk.parameter_source", "start_prepared_yalk_reference", {
            {"timeout_ms", std::to_string(natural(node, "timeout_ms", 3000))}}));
    if (!response.count("status") || response.at("status") != "ready") {
        throw std::runtime_error("Адаптер не выдал медленный кадр ЯЛК");
    }
    if (response.count("first_sequence")) context.state["yalk.first_sequence"] = response.at("first_sequence");
    return {RunVerdict::Ok, "Адаптер запущен в режиме ЯЛК; получен референсный кадр 204 байта", {}};
}

ProcedureResult yalkReadCalibration(const ScenarioNode& node, ProcedureContext& context)
{
    const bool confirmed = yalkBindingsConfirmed(context, natural(node, "channel_count", 80));
    if (!confirmed && argument(node, "commissioning", "false") != "true") {
        return {RunVerdict::Incomplete, "Калибровочные адреса ЯЛК не подтверждены", {}};
    }
    const auto first = resolveLogicalBinding(context, "yalk_voltage", 0);
    const auto zero = resolveLogicalBinding(context, "yalk_calibration_zero", 0);
    const auto full = resolveLogicalBinding(context, "yalk_calibration_full", 0);
    setYalkVoltage(context, first, number(node, "full_voltage", 6.2), true);
    wait(context, natural(node, "settle_ms", 150));
    const double v7 = readReferenceVoltage(context);
    unsigned sequence = ulkLastSequence(context);
    const auto zeroValue = readUlkChannel(context, static_cast<unsigned>(std::stoul(zero.locator)),
                                          natural(node, "sample_count", 16), sequence);
    const auto fullValue = readUlkChannel(context, static_cast<unsigned>(std::stoul(full.locator)),
                                          natural(node, "sample_count", 16), zeroValue.lastSequence);
    setYalkVoltage(context, first, 0.0, false);
    if (!(fullValue.code > zeroValue.code) || !std::isfinite(v7) || v7 < 5.5 || v7 > 6.8) {
        throw std::runtime_error("Недостоверная калибровка ЯЛК 97/99 или напряжение В7");
    }
    context.state["yalk.zero_code"] = std::to_string(zeroValue.code);
    context.state["yalk.full_code"] = std::to_string(fullValue.code);
    context.state["yalk.full_voltage"] = std::to_string(v7);
    ProcedureResult result{RunVerdict::Ok, "Снята калибровка ЯЛК по адресам 97/99", {}};
    auto value = measurement("ubsi.yalk.calibration", "Калибровочная шкала ЯЛК",
                             6.2, v7, 5.5, 6.8, "В");
    value.attributes = {{"zero_code", std::to_string(zeroValue.code)},
                        {"full_code", std::to_string(fullValue.code)},
                        {"zero_address", zero.locator}, {"full_address", full.locator}};
    append(result, std::move(value));
    markCommissioning(result, confirmed);
    return result;
}

double yalkCodeToVolts(double code, const ProcedureContext& context)
{
    const double zero = stateNumber(context, "yalk.zero_code");
    const double full = stateNumber(context, "yalk.full_code");
    const double voltage = stateNumber(context, "yalk.full_voltage");
    if (!(full > zero)) throw std::runtime_error("Неверная калибровка ЯЛК");
    return (code - zero) * voltage / (full - zero);
}

ProcedureResult yalkCheckInitial(const ScenarioNode& node, ProcedureContext& context)
{
    const unsigned count = natural(node, "channel_count", 80);
    const bool confirmed = yalkBindingsConfirmed(context, count);
    if (!confirmed && argument(node, "commissioning", "false") != "true") {
        return {RunVerdict::Incomplete, "Адреса ЯЛК не подтверждены", {}};
    }
    ProcedureResult result{RunVerdict::Ok, "Проверено исходное отключённое состояние ЯЛК", {}};
    context.equipment.invoke("stand.switch_matrix", "full_reset", {});
    unsigned sequence = ulkLastSequence(context);
    const double tolerance = number(node, "full_scale_v", 6.2)
        * number(node, "tolerance_percent_fs", 0.5) / 100.0;
    for (unsigned channel = 0; channel < count; ++channel) {
        const auto binding = resolveLogicalBinding(context, "yalk_voltage", channel);
        const unsigned address = static_cast<unsigned>(std::stoul(binding.locator));
        const auto reading = readUlkChannel(context, address,
                                            natural(node, "sample_count", 4), sequence);
        sequence = reading.lastSequence;
        const double volts = yalkCodeToVolts(reading.code, context);
        auto analog = measurement("ubsi.yalk.initial." + binding.locator,
            "ЯЛК адрес " + binding.locator + ": исходное напряжение",
            0.0, volts, -tolerance, tolerance, "В");
        analog.attributes = {{"ulk_address", binding.locator},
                             {"raw", std::to_string(reading.raw)},
                             {"analog_code", std::to_string(reading.code)},
                             {"signal", reading.signal ? "1" : "0"}};
        append(result, std::move(analog));
        append(result, measurement("ubsi.yalk.initial.signal." + binding.locator,
            "ЯЛК адрес " + binding.locator + ": исходный сигнал",
            1, reading.signal ? 1 : 0, 1, 1, "лог."));
    }
    markCommissioning(result, confirmed);
    return result;
}

ProcedureResult yalkCheckChannels(const ScenarioNode& node, ProcedureContext& context)
{
    const unsigned count = natural(node, "channel_count", 80);
    const bool confirmed = yalkBindingsConfirmed(context, count);
    if (!confirmed && argument(node, "commissioning", "false") != "true") {
        return {RunVerdict::Incomplete, "Адреса/маршруты ЯЛК не подтверждены", {}};
    }
    const auto pointVolts = numbers(node, "point_volts");
    if (pointVolts.size() != 3) {
        throw std::invalid_argument("Для ЯЛК требуются три точки напряжения");
    }
    const double fullScale = number(node, "full_scale_v", 6.2);
    const double tolerance = fullScale * number(node, "tolerance_percent_fs", 0.5) / 100.0;
    ProcedureResult result{RunVerdict::Ok, "Проверены 80 адресов ЯЛК", {}};
    for (unsigned channel = 0; channel < count; ++channel) {
        const auto binding = resolveLogicalBinding(context, "yalk_voltage", channel);
        const unsigned address = static_cast<unsigned>(std::stoul(binding.locator));
        for (std::size_t point = 0; point < pointVolts.size(); ++point) {
            setYalkVoltage(context, binding, pointVolts[point], true);
            wait(context, natural(node, "settle_ms", 150));
            const double v7 = readReferenceVoltage(context);
            const unsigned sequence = ulkLastSequence(context);
            const auto reading = readUlkChannel(context, address,
                                                natural(node, "sample_count", 16), sequence);
            const double volts = yalkCodeToVolts(reading.code, context);
            const double absolute = std::abs(volts - v7);
            const double reduced = absolute / fullScale * 100.0;
            auto analog = measurement("ubsi.yalk." + binding.locator + "." + std::to_string(point),
                "ЯЛК адрес " + binding.locator + ", " + std::to_string(pointVolts[point]) + " В",
                v7, volts, v7 - tolerance, v7 + tolerance, "В");
            analog.attributes = {
                {"ulk_address", binding.locator}, {"isd_channel", binding.locator},
                {"command_v", std::to_string(pointVolts[point])},
                {"raw", std::to_string(reading.raw)},
                {"analog_code", std::to_string(reading.code)},
                {"signal", reading.signal ? "1" : "0"}, {"v7_v", std::to_string(v7)},
                {"yalk_v", std::to_string(volts)}, {"absolute_error_v", std::to_string(absolute)},
                {"reduced_error_percent", std::to_string(reduced)},
                {"relative_error_percent", std::abs(v7) > 0.01
                    ? std::to_string(absolute / std::abs(v7) * 100.0) : ""}};
            const auto verdict = analog.verdict;
            context.eventSink({std::chrono::system_clock::now(), node.id, "MEASUREMENT",
                analog.title, verdict, analog.attributes});
            append(result, std::move(analog));
            if (point == 0 || point == 2) {
                const bool expected = point == 2;
                auto signal = measurement("ubsi.yalk.signal." + binding.locator + "." + std::to_string(point),
                    "ЯЛК адрес " + binding.locator + ": сигнальный признак",
                    expected ? 1 : 0, reading.signal ? 1 : 0,
                    expected ? 1 : 0, expected ? 1 : 0, "лог.");
                signal.attributes = {{"ulk_address", binding.locator},
                                     {"command_v", std::to_string(pointVolts[point])},
                                     {"signal", reading.signal ? "1" : "0"}};
                append(result, std::move(signal));
            }
        }
        setYalkVoltage(context, binding, 0.0, false);
    }
    markCommissioning(result, confirmed);
    return result;
}

ProcedureResult yalkCheckOverload(const ScenarioNode& node, ProcedureContext&)
{
    if (argument(node, "mapping_confirmed", "false") != "true") {
        return {RunVerdict::Incomplete,
            "Маршруты обрыва и ±12 В ещё не подтверждены на УБСИ; опасное воздействие не выполнялось", {}};
    }
    return {RunVerdict::Incomplete,
        "Алгоритм перегрузки разрешён профилем, но аппаратная карта поканальной коммутации ещё не зафиксирована", {}};
}

ProcedureResult yalkSafeCleanup(const ScenarioNode&, ProcedureContext& context)
{
    context.equipment.invoke("stand.switch_matrix", "full_reset", {});
    context.equipment.invoke("ulk.parameter_source", "stop_stream", {});
    context.equipment.invoke("ulk.parameter_source", "stop_record", {});
    return {RunVerdict::Ok, "ИСД сброшен, поток и запись адаптера остановлены", {}};
}

ProcedureResult referenceVoltage(const ScenarioNode& node, ProcedureContext& context)
{
    ProcedureResult result{RunVerdict::Ok, "Проверено эталонное напряжение", {}};
    const double nominal = number(node, "nominal_v", 6.2);
    const double tolerance = number(node, "tolerance_v", 0.03);
    const double v7 = readReferenceVoltage(context);
    append(result, measurement("ubsi.reference_6v2", "Эталонное напряжение по В7", nominal,
        v7, nominal - tolerance, nominal + tolerance, "В"));
    const std::string adapterGroup = argument(node, "adapter_parameter_group");
    if (!adapterGroup.empty()) {
        const std::string zeroGroup = argument(node, "calibration_zero_parameter", "yalk_calibration_zero");
        const std::string fullGroup = argument(node, "calibration_full_parameter", "yalk_calibration_full");
        if (!bindingsReady(context, adapterGroup, 1)
            || !bindingsReady(context, zeroGroup, 1)
            || !bindingsReady(context, fullGroup, 1)) {
            result.verdict = combineVerdicts(result.verdict, RunVerdict::Incomplete);
            result.message = "В7 проверен; адреса УЛК 97/98/99 ещё не подтверждены";
            return result;
        }
        const double zeroRaw = readLogicalParameter(context,
            zeroGroup, 0,
            natural(node, "sample_count", 16));
        const double fullRaw = readLogicalParameter(context,
            fullGroup, 0,
            natural(node, "sample_count", 16));
        const double raw = readLogicalParameter(context, adapterGroup, 0,
            natural(node, "sample_count", 16));
        if (!(fullRaw > zeroRaw)) throw std::runtime_error(
            "Некорректные калибровки ЯЛК для адреса УЛК 98");
        const double adapterVoltage = (raw - zeroRaw) * nominal / (fullRaw - zeroRaw);
        append(result, measurement("ubsi.reference_6v2.adapter",
            "Эталон 6,2 В по адресу УЛК 98", v7, adapterVoltage,
            v7 - tolerance, v7 + tolerance, "В"));
    }
    return result;
}

ProcedureResult ytp(const ScenarioNode& node, ProcedureContext& context)
{
    ProcedureResult result{RunVerdict::Ok, "Проверены температурные каналы ЯТП", {}};
    const unsigned count = natural(node, "channel_count", 30);
    const std::string parameterGroup = argument(node, "parameter_group", "ytp_temperature");
    const auto points = numbers(node, "resistance_points_ohm");
    double zeroRaw = number(node, "calibration_zero_raw");
    double fullRaw = number(node, "calibration_full_raw");
    const double fullScale = number(node, "full_scale_ohm", 240.0);
    const double tolerance = fullScale * number(node, "tolerance_percent_fs", 0.5) / 100.0;
    const std::string zeroParameter = argument(node, "calibration_zero_parameter");
    const std::string fullParameter = argument(node, "calibration_full_parameter");
    if (!bindingsReady(context, parameterGroup, count, true)
        || (!zeroParameter.empty() && !bindingsReady(context, zeroParameter, 1))
        || (!fullParameter.empty() && !bindingsReady(context, fullParameter, 1))) {
        return {RunVerdict::Incomplete,
            "Адреса УЛК/маршруты ЯТП и калибровки 32/31 не подтверждены; ручные точки не запрашивались", {}};
    }
    if (!zeroParameter.empty() && !fullParameter.empty()) {
        zeroRaw = readLogicalParameter(context, zeroParameter, 0,
            natural(node, "sample_count", 16));
        fullRaw = readLogicalParameter(context, fullParameter, 0,
            natural(node, "sample_count", 16));
    }
    if (points.empty() || !(fullRaw > zeroRaw)) throw std::invalid_argument("Не заполнена карта/калибровка ЯТП");
    for (const double resistance : points) {
        const auto confirmation = responseValues(context.equipment.invoke(
            "operator.manual_input", "confirm_value", {
                {"title", "Р4831: установите сопротивление для проверки ЯТП"},
                {"target_value", std::to_string(resistance)}, {"unit", "Ом"}}));
        const auto actualValue = confirmation.find("value");
        if (actualValue == confirmation.end()) {
            throw std::runtime_error("Ручной этап Р4831 не вернул фактическое сопротивление");
        }
        const double actualResistance = std::stod(actualValue->second);
        MeasurementResult audit = measurement("ubsi.ytp.manual_reference",
            "Р4831: подтверждённая ручная точка", resistance, actualResistance,
            0.0, fullScale, "Ом");
        audit.message = "Оператор=" + (confirmation.count("operator")
            ? confirmation.at("operator") : std::string("не указан"))
            + "; время=" + (confirmation.count("timestamp")
                ? confirmation.at("timestamp") : std::string("не указано"));
        append(result, std::move(audit));
        wait(context, natural(node, "settle_ms", 1500));
        for (unsigned channel = 0; channel < count; ++channel) {
            const auto binding = resolveLogicalBinding(context, parameterGroup, channel);
            if (binding.stimulusRoute.empty()) throw std::runtime_error(
                "В БД не задан маршрут ЯТП для канала " + std::to_string(channel + 1));
            context.equipment.invoke("stand.switch_matrix", "switch", {
                {"route", binding.stimulusRoute}, {"offset", std::to_string(binding.stimulusOffset)},
                {"enabled", "true"}});
            const double raw = readLogicalParameter(
                context, parameterGroup, channel, natural(node, "sample_count", 16));
            const double measured = (raw - zeroRaw) * fullScale / (fullRaw - zeroRaw);
            append(result, measurement("ubsi.ytp." + std::to_string(channel + 1),
                "ЯТП канал " + std::to_string(channel + 1), actualResistance, measured,
                actualResistance - tolerance, actualResistance + tolerance, "Ом"));
            context.equipment.invoke("stand.switch_matrix", "switch", {
                {"route", binding.stimulusRoute}, {"offset", std::to_string(binding.stimulusOffset)},
                {"enabled", "false"}});
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
    const std::string parameterGroup = argument(node, "parameter_group", "yvp_fast");
    if (frequencies.empty() || gains.empty()) throw std::invalid_argument("Не заданы частоты и коэффициенты ЯВП");
    if (!bindingsReady(context, parameterGroup, channels, true)) {
        return {RunVerdict::Incomplete,
            "Адреса Орбиты/маршруты ЯВП-8 не подтверждены; генератор не включался", {}};
    }
    const double amplitude = number(node, "amplitude_vpp", 0.3875);
    for (unsigned channel = 1; channel <= channels; ++channel) {
        const std::string inputRoute = argument(node, "input_route", "yvp_input");
        context.equipment.invoke("stand.switch_matrix", "switch", {
            {"route", inputRoute}, {"offset", std::to_string(channel - 1)}, {"enabled", "true"}});
        for (const double frequency : frequencies) {
            context.equipment.invoke("signal.generator", "set_sine", {
                {"channel", "1"}, {"frequency_hz", std::to_string(frequency)},
                {"amplitude_vpp", std::to_string(amplitude)}, {"offset_v", "0"}});
            context.equipment.invoke("signal.generator", "output", {{"channel", "1"}, {"enabled", "true"}});
            wait(context, natural(node, "settle_ms", 200));
            const double measuredFrequency = readReferenceFrequency(context);
            const double measuredRms = readReferenceAcVoltage(context);
            const double referenceVpp = measuredRms * 2.0 * std::sqrt(2.0);
            const double orbitaValue = readLogicalParameter(
                context, parameterGroup, channel - 1, natural(node, "sample_count", 16));
            const double frequencyTolerance = number(node, "frequency_tolerance_percent", 1.0);
            append(result, measurement("ubsi.yvp.frequency." + std::to_string(channel),
                "ЯВП " + std::to_string(channel) + ": частота воздействия",
                frequency, measuredFrequency, frequency * (1.0 - frequencyTolerance / 100.0),
                frequency * (1.0 + frequencyTolerance / 100.0), "Гц"));
            const double stimulusTolerance = number(node, "stimulus_tolerance_percent", 5.0);
            append(result, measurement("ubsi.yvp.stimulus." + std::to_string(channel),
                "ЯВП " + std::to_string(channel) + ": воздействие по В7",
                amplitude, referenceVpp, amplitude * (1.0 - stimulusTolerance / 100.0),
                amplitude * (1.0 + stimulusTolerance / 100.0), "В пик-пик"));
            const double percent = std::abs(frequency - 2.0) < 0.001 || std::abs(frequency - 2000.0) < 0.001
                ? number(node, "edge_tolerance_percent", 10.0) : number(node, "middle_tolerance_percent", 5.0);
            append(result, measurement("ubsi.yvp." + std::to_string(channel),
                "ЯВП " + std::to_string(channel) + ", " + std::to_string(frequency) + " Гц",
                referenceVpp, orbitaValue, referenceVpp * (1.0 - percent / 100.0),
                referenceVpp * (1.0 + percent / 100.0), "В пик-пик"));
        }
        if (argument(node, "gain_conversion_confirmed", "false") == "true") {
            for (std::size_t gainIndex = 0; gainIndex < gains.size(); ++gainIndex) {
            // Выбор усиления выполняется внешней кроссировкой ИСД; профиль задаёт
            // базовый канал и один шаг на значение ряда.
            context.equipment.invoke("stand.switch_matrix", "switch", {
                {"route", argument(node, "gain_route", "yvp_gain")},
                 {"offset", std::to_string((channel - 1) * gains.size() + gainIndex)}, {"enabled", "true"}});
            // Формула будет включена только вместе с подтверждённым преобразованием
            // конкретного адреса Орбиты; до этого ветка недоступна из published YAML.
            throw std::runtime_error("Для коэффициентов ЯВП не задан подтверждённый пересчёт");
            context.equipment.invoke("stand.switch_matrix", "switch", {
                {"route", argument(node, "gain_route", "yvp_gain")},
                 {"offset", std::to_string((channel - 1) * gains.size() + gainIndex)}, {"enabled", "false"}});
            }
        } else {
            result.verdict = combineVerdicts(result.verdict, RunVerdict::Incomplete);
            result.message = "АЧХ измерена; коэффициенты ЯВП не коммутировались: "
                "пересчёт данных Орбиты по референсу KPA ещё не подтверждён";
        }
        context.equipment.invoke("stand.switch_matrix", "switch", {
            {"route", inputRoute}, {"offset", std::to_string(channel - 1)}, {"enabled", "false"}});
    }
    context.equipment.invoke("signal.generator", "output", {{"channel", "1"}, {"enabled", "false"}});
    return result;
}

} // namespace

void registerUbsiProcedures(ScenarioEngine& engine)
{
    engine.registerProcedure("ubsi.binding_check", bindingCheck);
    engine.registerProcedure("ubsi.readiness", readiness);
    engine.registerProcedure("ubsi.supply_range", supplyRange);
    engine.registerProcedure("ubsi.sensor_supply", sensorSupply);
    engine.registerProcedure("ubsi.yalk_analog", yalkAnalog);
    engine.registerProcedure("ubsi.yalk_contacts", yalkContacts);
    engine.registerProcedure("ubsi.yalk_faults", yalkFaults);
    engine.registerProcedure("yalk.start_stream", yalkStartStream);
    engine.registerProcedure("yalk.read_calibration", yalkReadCalibration);
    engine.registerProcedure("yalk.check_initial_state", yalkCheckInitial);
    engine.registerProcedure("yalk.check_channels", yalkCheckChannels);
    engine.registerProcedure("yalk.check_overload", yalkCheckOverload);
    engine.registerProcedure("yalk.safe_cleanup", yalkSafeCleanup);
    engine.registerProcedure("ubsi.reference_voltage", referenceVoltage);
    engine.registerProcedure("ubsi.ytp", ytp);
    engine.registerProcedure("ubsi.yvp", yvp);
}

} // namespace orbita::stand
