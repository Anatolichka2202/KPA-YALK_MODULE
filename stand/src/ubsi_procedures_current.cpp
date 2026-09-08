#include "orbita_stand/ubsi_procedures.h"
#include "orbita_stand/ubsi_yvp_math.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace orbita::stand {

void registerLegacyUbsiProcedures(ScenarioEngine& engine);

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

double number(const ScenarioNode& node, const std::string& key, double fallback = 0.0)
{
    const auto text = argument(node, key);
    if (text.empty()) return fallback;
    std::size_t parsed = 0;
    const double value = std::stod(text, &parsed);
    if (parsed != text.size() || !std::isfinite(value))
        throw std::invalid_argument("Некорректный аргумент " + key);
    return value;
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

bool flag(const ScenarioNode& node, const std::string& key, bool fallback = false)
{
    const auto text = argument(node, key);
    if (text.empty()) return fallback;
    if (text == "true" || text == "1" || text == "yes") return true;
    if (text == "false" || text == "0" || text == "no") return false;
    throw std::invalid_argument("Некорректный логический аргумент " + key);
}

std::vector<double> numbers(const ScenarioNode& node, const std::string& key)
{
    std::vector<double> result;
    std::stringstream stream(argument(node, key));
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) result.push_back(std::stod(item));
    }
    return result;
}

double responseNumber(const std::string& response, const std::string& key)
{
    const auto values = responseValues(response);
    const auto found = values.find(key);
    if (found == values.end()) throw std::runtime_error("Оборудование не вернуло поле " + key);
    return std::stod(found->second);
}

unsigned responseUnsigned(const std::string& response, const std::string& key)
{
    const auto values = responseValues(response);
    const auto found = values.find(key);
    if (found == values.end()) throw std::runtime_error("Оборудование не вернуло поле " + key);
    return static_cast<unsigned>(std::stoul(found->second));
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

void wait(ProcedureContext& context, unsigned milliseconds)
{
    constexpr unsigned slice = 50;
    for (unsigned elapsed = 0; elapsed < milliseconds; elapsed += slice) {
        if (context.stopRequested.load()) throw std::runtime_error("Остановлено оператором");
        std::this_thread::sleep_for(std::chrono::milliseconds(
            std::min(slice, milliseconds - elapsed)));
    }
}

bool restartYalk(ProcedureContext& context, unsigned timeoutMs)
{
    const auto started = std::chrono::steady_clock::now();
    do {
        if (context.stopRequested.load()) throw std::runtime_error("Остановлено оператором");
        try {
            context.equipment.invoke("ulk.parameter_source", "prepare_yalk_reference", {});
            const auto response = responseValues(context.equipment.invoke(
                "ulk.parameter_source", "start_prepared_yalk_reference", {
                    {"timeout_ms", std::to_string(std::min(timeoutMs, 1000u))}}));
            if (response.count("status") && response.at("status") == "ready") return true;
        } catch (const std::exception&) {
        }
        const auto elapsed = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
        if (elapsed >= timeoutMs) return false;
        wait(context, std::min(250u, timeoutMs - elapsed));
    } while (true);
}

ProcedureResult sensorSupplyDisabled(const ScenarioNode&, ProcedureContext&)
{
    return {RunVerdict::Incomplete,
        "Автоматическая поканальная проверка тока/нагрузок 350/450 мА исключена из текущей методики; "
        "опасное воздействие не выполнялось", {}};
}

ProcedureResult supplyRangeCurrent(const ScenarioNode& node, ProcedureContext& context)
{
    const auto points = numbers(node, "voltage_points_v");
    if (points.empty()) throw std::invalid_argument("Для проверки питания нужны voltage_points_v");
    const auto survival = numbers(node, "survival_points_v");
    const auto durations = numbers(node, "survival_seconds");
    if (survival.size() != durations.size())
        throw std::invalid_argument("survival_points_v и survival_seconds должны совпадать");

    const double hardwareCurrentLimit = number(node, "supply_current_limit_a", 0.6);
    const double totalCurrentLimit = number(node, "maximum_total_current_a",
        number(node, "current_limit_a", 0.4));
    const double voltageTolerance = number(node, "voltage_tolerance_v", 0.5);
    const double restoreVoltage = number(node, "restore_voltage_v", 27.0);
    const unsigned settleMs = natural(node, "settle_ms", 500);
    const unsigned recoveryTimeoutMs = natural(node, "recovery_timeout_ms", 30000);

    ProcedureResult result{RunVerdict::Ok,
        "Проверены рабочий диапазон питания и общий ток потребления УБСИ", {}};
    const auto restore = [&] {
        context.equipment.invoke("power.dc_supply", "set_voltage", {
            {"volts", std::to_string(restoreVoltage)}});
        wait(context, settleMs);
    };

    try {
        context.equipment.invoke("power.dc_supply", "set_current_limit", {
            {"amperes", std::to_string(hardwareCurrentLimit)}});
        context.equipment.invoke("power.dc_supply", "output", {{"enabled", "true"}});

        for (const double setpoint : points) {
            context.equipment.invoke("power.dc_supply", "set_voltage", {
                {"volts", std::to_string(setpoint)}});
            wait(context, settleMs);
            const auto state = context.equipment.invoke("power.dc_supply", "read_state", {});
            const double actualVoltage = responseNumber(state, "volts");
            const double totalCurrent = responseNumber(state, "amperes");

            auto voltage = measurement("ubsi.supply.voltage",
                "Фактическое питание при уставке " + std::to_string(setpoint) + " В",
                setpoint, actualVoltage, setpoint - voltageTolerance,
                setpoint + voltageTolerance, "В");
            voltage.attributes = {{"setpoint_v", std::to_string(setpoint)},
                                  {"supply_current_a", std::to_string(totalCurrent)}};
            append(result, std::move(voltage));

            auto current = measurement("ubsi.supply.total_current",
                "Общий ток потребления УБСИ при " + std::to_string(setpoint) + " В",
                0.0, totalCurrent, 0.0, totalCurrentLimit, "А");
            current.attributes = {{"setpoint_v", std::to_string(setpoint)},
                                  {"measurement_scope", "whole_ubsi"},
                                  {"hardware_current_limit_a", std::to_string(hardwareCurrentLimit)}};
            append(result, std::move(current));

            const auto alive = responseValues(context.equipment.invoke(
                "ulk.parameter_source", "alive", {{"timeout_ms", "3000"}}));
            append(result, measurement("ubsi.supply.data_alive",
                "Передача данных при " + std::to_string(setpoint) + " В", 1.0,
                alive.count("status") && alive.at("status") == "ready" ? 1.0 : 0.0,
                1.0, 1.0, "лог."));
        }

        for (std::size_t index = 0; index < survival.size(); ++index) {
            context.equipment.invoke("power.dc_supply", "set_voltage", {
                {"volts", std::to_string(survival[index])}});
            wait(context, static_cast<unsigned>(durations[index] * 1000.0));
            const auto state = context.equipment.invoke("power.dc_supply", "read_state", {});
            const double actualVoltage = responseNumber(state, "volts");
            auto held = measurement("ubsi.supply.survival_voltage",
                "Фактическое напряжение выдержки " + std::to_string(survival[index]) + " В",
                survival[index], actualVoltage, survival[index] - voltageTolerance,
                survival[index] + voltageTolerance, "В");
            held.attributes = {{"duration_s", std::to_string(durations[index])}};
            append(result, std::move(held));

            restore();
            append(result, measurement("ubsi.supply.survived",
                "Работоспособность после предельного напряжения и возврата к 27 В",
                1.0, restartYalk(context, recoveryTimeoutMs) ? 1.0 : 0.0,
                1.0, 1.0, "лог."));
        }
        restore();
    } catch (...) {
        try { restore(); } catch (...) {}
        throw;
    }
    return result;
}

struct YvpBinding {
    std::string source;
    std::string locatorType;
    unsigned address = 0;
    bool confirmed = false;
};

YvpBinding resolveYvpBinding(ProcedureContext& context, const std::string& group,
                             unsigned channel)
{
    const auto values = responseValues(context.equipment.invoke(
        "catalog.parameter_resolver", "resolve", {
            {"block_type", "UBSI_468157_002"},
            {"parameter_group", group},
            {"channel_index", std::to_string(channel)}}));
    const auto get = [&values](const char* key) -> std::string {
        const auto found = values.find(key);
        if (found == values.end()) throw std::runtime_error(std::string("Каталог не вернул ") + key);
        return found->second;
    };
    return {get("source"), get("locator_type"),
            static_cast<unsigned>(std::stoul(get("locator"))),
            get("confirmed") == "true"};
}

double stateNumber(const ProcedureContext& context, const std::string& key)
{
    const auto found = context.state.find(key);
    if (found == context.state.end()) throw std::runtime_error("Нет состояния ЯЛК: " + key);
    return std::stod(found->second);
}

struct YvpSample {
    double code = 0.0;
    double volts = 0.0;
    unsigned firstSequence = 0;
    unsigned lastSequence = 0;
    std::string rawSamples;
    std::string codeSamples;
};

YvpSample readYvpSample(ProcedureContext& context, unsigned address, unsigned sampleCount)
{
    const unsigned after = responseUnsigned(
        context.equipment.invoke("ulk.parameter_source", "stats", {}), "last_sequence");
    const auto response = context.equipment.invoke("ulk.parameter_source", "read_channel", {
        {"ulk_address", std::to_string(address)},
        {"sample_count", std::to_string(sampleCount)},
        {"after_sequence", std::to_string(after)},
        {"timeout_ms", "3000"}});
    const auto values = responseValues(response);
    const double code = responseNumber(response, "analog_code_mean");
    const double zero = stateNumber(context, "yalk.zero_code");
    const double full = stateNumber(context, "yalk.full_code");
    const double fullVoltage = stateNumber(context, "yalk.full_voltage");
    if (!(full > zero)) throw std::runtime_error("Некорректная калибровка ЯЛК 97/99");
    return {code, (code - zero) * fullVoltage / (full - zero),
            responseUnsigned(response, "first_sequence"),
            responseUnsigned(response, "last_sequence"),
            values.count("raw_samples") ? values.at("raw_samples") : std::string(),
            values.count("analog_code_samples") ? values.at("analog_code_samples") : std::string()};
}

void requireInstrumentPoint(double commandedFrequency, double measuredFrequency,
                            double commandedVpp, double measuredVpp,
                            double frequencyTolerancePercent,
                            double stimulusTolerancePercent)
{
    if (!(measuredFrequency > 0.0) || !(measuredVpp > 0.0))
        throw std::runtime_error("В7 не подтвердил частоту/амплитуду воздействия ЯВП");
    if (std::abs(measuredFrequency - commandedFrequency) / commandedFrequency * 100.0
        > frequencyTolerancePercent)
        throw std::runtime_error("Фактическая частота воздействия ЯВП не соответствует уставке");
    if (std::abs(measuredVpp - commandedVpp) / commandedVpp * 100.0
        > stimulusTolerancePercent)
        throw std::runtime_error("Фактическая амплитуда воздействия ЯВП не соответствует уставке");
}

ProcedureResult yvpCurrent(const ScenarioNode& node, ProcedureContext& context)
{
    const unsigned channels = natural(node, "channel_count", 8);
    const unsigned sampleCount = natural(node, "sample_count", 16);
    const auto gains = numbers(node, "gains_mv_per_pcl");
    const auto frequencies = numbers(node, "frequencies_hz");
    const std::string group = argument(node, "parameter_group", "yvp_fast");
    const unsigned addressMin = natural(node, "yalk_address_min", 88);
    const unsigned addressMax = natural(node, "yalk_address_max", 96);
    if (channels != 8 || sampleCount < 16 || gains.empty() || frequencies.empty())
        throw std::invalid_argument("ЯВП: требуется 8 каналов, >=16 свежих кадров, ряд коэффициентов и частот");
    if (addressMin > addressMax)
        throw std::invalid_argument("ЯВП: yalk_address_min больше yalk_address_max");

    std::vector<YvpBinding> bindings;
    std::set<unsigned> addresses;
    bindings.reserve(channels);
    for (unsigned channel = 0; channel < channels; ++channel) {
        const auto binding = resolveYvpBinding(context, group, channel);
        if (!binding.confirmed || binding.source != "ulk.parameter_source"
            || binding.locatorType != "ulk_address"
            || binding.address < addressMin || binding.address > addressMax
            || !addresses.insert(binding.address).second) {
            return {RunVerdict::Incomplete,
                "ЯВП-8 должен читаться по восьми уникальным адресам ЯЛК внутри подтверждённого диапазона 88–96; генератор не включался", {}};
        }
        bindings.push_back(binding);
    }
    if (!addresses.count(addressMin) || !addresses.count(addressMax)) {
        return {RunVerdict::Incomplete,
            "Диапазон ЯВП исправлен на ЯЛК 88–96, но точная восьмиканальная карта внутри диапазона ещё не подтверждена каталогом; генератор не включался", {}};
    }

    if (!flag(node, "routes_confirmed") || !flag(node, "yalk_value_model_confirmed")) {
        return {RunVerdict::Incomplete,
            "Адресный диапазон ЯВП 88–96 подтверждён, но активная кроссировка/модель Uout ещё не подтверждены на стенде; генератор не включался", {}};
    }

    const double gainTestFrequency = number(node, "gain_test_frequency_hz");
    const double afcReferenceFrequency = number(node, "afc_reference_frequency_hz");
    const double workingLow = number(node, "working_low_frequency_hz");
    const double workingHigh = number(node, "working_high_frequency_hz");
    const double attenuationMinDb = number(node, "attenuation_min_db");
    if (!(gainTestFrequency > 0.0) || !(afcReferenceFrequency > 0.0)
        || !(workingLow > 0.0) || !(workingHigh > workingLow)
        || !(attenuationMinDb > 0.0)) {
        return {RunVerdict::Incomplete,
            "Алгоритм ЯВП готов, но опорная частота/граница затухания не подтверждены конфигурацией; генератор не включался", {}};
    }

    const double capacitancePf = number(node, "coupling_capacitance_pf", 1000.0);
    const double gainTolerance = number(node, "gain_tolerance_percent", 7.0);
    const double frequencyTolerance = number(node, "frequency_tolerance_percent", 1.0);
    const double stimulusTolerance = number(node, "stimulus_tolerance_percent", 5.0);
    const unsigned settleMs = natural(node, "settle_ms", 200);
    const std::string inputRoute = argument(node, "input_route", "yvp_input");
    const std::string gainRoute = argument(node, "gain_route", "yvp_gain");

    const auto gainOne = std::find_if(gains.begin(), gains.end(), [](double value) {
        return std::abs(value - 1.0) < 1e-9;
    });
    if (gainOne == gains.end()) throw std::invalid_argument("В ряду ЯВП отсутствует X1 = 1 мВ/пКл");
    const std::size_t gainOneIndex = static_cast<std::size_t>(gainOne - gains.begin());

    ProcedureResult result{RunVerdict::Ok, "Проверены ЯВП-8 по настроенной карте в диапазоне ЯЛК 88–96", {}};
    auto cleanup = [&] {
        try { context.equipment.invoke("signal.generator", "output", {{"channel", "1"}, {"enabled", "false"}}); } catch (...) {}
        for (unsigned channel = 0; channel < channels; ++channel) {
            try { context.equipment.invoke("stand.switch_matrix", "switch", {
                {"route", inputRoute}, {"offset", std::to_string(channel)}, {"enabled", "false"}}); } catch (...) {}
            for (std::size_t gainIndex = 0; gainIndex < gains.size(); ++gainIndex) {
                try { context.equipment.invoke("stand.switch_matrix", "switch", {
                    {"route", gainRoute},
                    {"offset", std::to_string(channel * gains.size() + gainIndex)},
                    {"enabled", "false"}}); } catch (...) {}
            }
        }
        try { context.equipment.invoke("stand.switch_matrix", "full_reset", {}); } catch (...) {}
    };

    const auto selectGain = [&](unsigned channel, std::size_t index, bool enabled) {
        context.equipment.invoke("stand.switch_matrix", "switch", {
            {"route", gainRoute},
            {"offset", std::to_string(channel * gains.size() + index)},
            {"enabled", enabled ? "true" : "false"}});
    };

    const auto setStimulus = [&](double frequency, double vpp) {
        context.equipment.invoke("signal.generator", "set_sine", {
            {"channel", "1"}, {"frequency_hz", std::to_string(frequency)},
            {"amplitude_vpp", std::to_string(vpp)}, {"offset_v", "0"}});
        context.equipment.invoke("signal.generator", "output", {
            {"channel", "1"}, {"enabled", "true"}});
        wait(context, settleMs);
        const double actualFrequency = responseNumber(context.equipment.invoke(
            "measure.reference_frequency", "read_frequency", {}), "hertz");
        const double actualRms = responseNumber(context.equipment.invoke(
            "measure.reference_ac_voltage", "read_ac_voltage", {}), "volts");
        const double actualVpp = actualRms * 2.0 * std::sqrt(2.0);
        requireInstrumentPoint(frequency, actualFrequency, vpp, actualVpp,
                               frequencyTolerance, stimulusTolerance);
        return std::pair<double, double>{actualFrequency, actualVpp};
    };

    try {
        for (unsigned channel = 0; channel < channels; ++channel) {
            const unsigned address = bindings[channel].address;
            context.equipment.invoke("stand.switch_matrix", "switch", {
                {"route", inputRoute}, {"offset", std::to_string(channel)}, {"enabled", "true"}});

            for (std::size_t gainIndex = 0; gainIndex < gains.size(); ++gainIndex) {
                const double gain = gains[gainIndex];
                const double commandedVpp = yvpStimulusVppForGain(gain);
                selectGain(channel, gainIndex, true);
                const auto actual = setStimulus(gainTestFrequency, commandedVpp);
                const auto sample = readYvpSample(context, address, sampleCount);
                const double chargePc = yvpChargePc(capacitancePf, actual.second);
                const double measuredGain = yvpGainMvPerPc(sample.volts, chargePc);
                const double errorPercent = yvpRelativeErrorPercent(measuredGain, gain);
                auto value = measurement("ubsi.yvp.gain." + std::to_string(channel + 1),
                    "ЯВП " + std::to_string(channel + 1) + ": K=" + std::to_string(gain) + " мВ/пКл",
                    gain, measuredGain, gain * (1.0 - gainTolerance / 100.0),
                    gain * (1.0 + gainTolerance / 100.0), "мВ/пКл");
                value.attributes = {{"yalk_address", std::to_string(address)},
                                    {"word_index", std::to_string(address - 1)},
                                    {"frequency_hz", std::to_string(actual.first)},
                                    {"input_vpp_v", std::to_string(actual.second)},
                                    {"capacitance_pf", std::to_string(capacitancePf)},
                                    {"charge_pc", std::to_string(chargePc)},
                                    {"output_v", std::to_string(sample.volts)},
                                    {"gain_error_percent", std::to_string(errorPercent)},
                                    {"raw_samples", sample.rawSamples},
                                    {"analog_code_samples", sample.codeSamples},
                                    {"first_sequence", std::to_string(sample.firstSequence)},
                                    {"last_sequence", std::to_string(sample.lastSequence)}};
                append(result, std::move(value));
                selectGain(channel, gainIndex, false);
            }

            selectGain(channel, gainOneIndex, true);
            const double afcVpp = yvpStimulusVppForGain(1.0);
            std::map<double, double> amplitudes;
            std::map<double, std::map<std::string, std::string>> attributes;
            for (const double frequency : frequencies) {
                const auto actual = setStimulus(frequency, afcVpp);
                const auto sample = readYvpSample(context, address, sampleCount);
                amplitudes[frequency] = sample.volts;
                attributes[frequency] = {{"yalk_address", std::to_string(address)},
                                         {"word_index", std::to_string(address - 1)},
                                         {"set_frequency_hz", std::to_string(frequency)},
                                         {"actual_frequency_hz", std::to_string(actual.first)},
                                         {"input_vpp_v", std::to_string(actual.second)},
                                         {"output_v", std::to_string(sample.volts)},
                                         {"raw_samples", sample.rawSamples},
                                         {"analog_code_samples", sample.codeSamples}};
            }
            const auto reference = amplitudes.find(afcReferenceFrequency);
            if (reference == amplitudes.end() || !(reference->second > 0.0))
                throw std::runtime_error("Опорная частота АЧХ отсутствует в frequencies_hz или дала нулевую амплитуду");

            for (const auto& [frequency, amplitude] : amplitudes) {
                if (frequency >= 2.0 * workingHigh) {
                    const double db = yvpAttenuationDb(reference->second, amplitude);
                    auto value = measurement("ubsi.yvp.attenuation." + std::to_string(channel + 1),
                        "ЯВП " + std::to_string(channel + 1) + ": затухание " + std::to_string(frequency) + " Гц",
                        attenuationMinDb, db, attenuationMinDb, 1.0e9, "дБ");
                    value.attributes = attributes.at(frequency);
                    value.attributes["reference_frequency_hz"] = std::to_string(afcReferenceFrequency);
                    value.attributes["reference_amplitude_v"] = std::to_string(reference->second);
                    append(result, std::move(value));
                    continue;
                }

                const double afc = yvpAfcPercent(amplitude, reference->second);
                double tolerance = 0.0;
                bool normative = true;
                if (frequency >= workingLow && frequency <= 3.0 * workingLow) tolerance = 10.0;
                else if (frequency > 3.0 * workingLow && frequency < 0.9 * workingHigh) tolerance = 5.0;
                else if (frequency >= 0.9 * workingHigh && frequency <= workingHigh) tolerance = 10.0;
                else normative = false;

                MeasurementResult value;
                if (normative) {
                    value = measurement("ubsi.yvp.afc." + std::to_string(channel + 1),
                        "ЯВП " + std::to_string(channel + 1) + ": АЧХ " + std::to_string(frequency) + " Гц",
                        0.0, afc, -tolerance, tolerance, "%");
                } else {
                    value.parameterKey = "ubsi.yvp.afc.extra." + std::to_string(channel + 1);
                    value.title = "ЯВП " + std::to_string(channel + 1) + ": дополнительная точка " + std::to_string(frequency) + " Гц";
                    value.reference = 0.0;
                    value.measured = afc;
                    value.lowerLimit = -1.0e9;
                    value.upperLimit = 1.0e9;
                    value.unit = "%";
                    value.verdict = RunVerdict::Ok;
                }
                value.attributes = attributes.at(frequency);
                value.attributes["reference_frequency_hz"] = std::to_string(afcReferenceFrequency);
                value.attributes["reference_amplitude_v"] = std::to_string(reference->second);
                value.attributes["normative"] = normative ? "true" : "false";
                append(result, std::move(value));
            }

            selectGain(channel, gainOneIndex, false);
            context.equipment.invoke("stand.switch_matrix", "switch", {
                {"route", inputRoute}, {"offset", std::to_string(channel)}, {"enabled", "false"}});
        }
        cleanup();
    } catch (...) {
        cleanup();
        throw;
    }
    return result;
}

} // namespace

void registerUbsiProcedures(ScenarioEngine& engine)
{
    registerLegacyUbsiProcedures(engine);
    // Current-delivery overrides. ScenarioEngine::registerProcedure replaces
    // the legacy callback with the same id.
    engine.registerProcedure("ubsi.sensor_supply", sensorSupplyDisabled);
    engine.registerProcedure("ubsi.supply_range", supplyRangeCurrent);
    engine.registerProcedure("ubsi.yvp", yvpCurrent);
}

} // namespace orbita::stand
