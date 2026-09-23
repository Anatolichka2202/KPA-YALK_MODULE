#include "ktma/ubsi/procedures.h"
#include "registration_layers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

bool flag(const ScenarioNode& node, const std::string& key, bool fallback = false)
{
    const auto text = argument(node, key);
    if (text.empty()) return fallback;
    if (text == "true" || text == "1" || text == "yes" || text == "on") return true;
    if (text == "false" || text == "0" || text == "no" || text == "off") return false;
    throw std::invalid_argument("Некорректный логический аргумент " + key);
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

std::vector<double> numbers(const ScenarioNode& node, const std::string& key)
{
    std::vector<double> result;
    std::stringstream stream(argument(node, key));
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

std::vector<unsigned> unsigneds(const std::string& text, bool allowNone = false)
{
    if (allowNone && (text.empty() || text == "none" || text == "-")) return {};
    std::vector<unsigned> result;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (item.empty()) continue;
        std::size_t parsed = 0;
        const auto value = std::stoul(item, &parsed, 0);
        if (parsed != item.size() || value == 0)
            throw std::invalid_argument("ЯВП: некорректный номер контакта");
        result.push_back(static_cast<unsigned>(value));
    }
    if (!allowNone && result.empty())
        throw std::invalid_argument("ЯВП: пустая карта контактов");
    return result;
}

std::string gainKey(double gain)
{
    if (std::abs(gain - 0.25) < 1e-9) return "gain_0_25";
    if (std::abs(gain - 0.5) < 1e-9) return "gain_0_5";
    if (std::abs(gain - 1.0) < 1e-9) return "gain_1";
    if (std::abs(gain - 2.0) < 1e-9) return "gain_2";
    if (std::abs(gain - 4.0) < 1e-9) return "gain_4";
    if (std::abs(gain - 8.0) < 1e-9) return "gain_8";
    if (std::abs(gain - 32.0) < 1e-9) return "gain_32";
    throw std::invalid_argument("ЯВП: коэффициент вне ряда 0.25/0.5/1/2/4/8/32");
}

bool hasArgument(const ScenarioNode& node, const std::string& key)
{
    const auto found = node.arguments.find(key);
    return found != node.arguments.end() && !found->second.empty();
}

double responseNumber(const std::string& response, const std::string& key)
{
    const auto values = responseValues(response);
    const auto found = values.find(key);
    if (found == values.end()) throw std::runtime_error("Оборудование не вернуло поле " + key);
    const double value = std::stod(found->second);
    if (!std::isfinite(value)) throw std::runtime_error("Некорректное поле оборудования " + key);
    return value;
}

void waitChecked(ProcedureContext& context, unsigned milliseconds)
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

void publish(ProcedureContext& context, const ScenarioNode& node,
             std::string stage, std::string message,
             RunVerdict verdict = RunVerdict::NotRun,
             std::map<std::string, std::string> data = {})
{
    if (!context.eventSink) return;
    context.eventSink({std::chrono::system_clock::now(), node.id,
        std::move(stage), std::move(message), verdict, std::move(data)});
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
    result.message = result.verdict == RunVerdict::Ok ? "Норма" : "Значение вне допуска";
    return result;
}

void append(ProcedureResult& result, MeasurementResult value)
{
    result.verdict = combineVerdicts(result.verdict, value.verdict);
    result.measurements.push_back(std::move(value));
}

double relativeError(double measured, double reference)
{
    if (!std::isfinite(measured) || !std::isfinite(reference) || reference == 0.0)
        throw std::invalid_argument("ЯВП: некорректный относительный допуск");
    return (measured - reference) / reference * 100.0;
}

double attenuationDb(double reference, double value)
{
    if (!(reference > 0.0) || !(value > 0.0))
        throw std::runtime_error("ЯВП: нельзя вычислить затухание из неположительной амплитуды");
    return 20.0 * std::log10(reference / value);
}

struct Observation {
    double calculatedGain = 0.0;
    double measuredRms = 0.0;
    double outputVpp = 0.0;
    double inputVpp = 0.0;
};

ProcedureResult yvpV7Isd(const ScenarioNode& node, ProcedureContext& context)
{
    const unsigned channelCount = natural(node, "channel_count", 8);
    const auto gains = numbers(node, "gains_mv_per_pcl");
    const auto frequencies = numbers(node, "frequencies_hz");
    if (channelCount != 8)
        throw std::invalid_argument("ЯВП-8: подтверждённый стендовый тракт содержит ровно 8 каналов");
    if (gains.empty() || frequencies.empty())
        throw std::invalid_argument("ЯВП-8: не заданы коэффициенты усиления или частоты");
    for (const double gain : gains) (void)gainKey(gain);

    // Fail-safe before the first physical mutation. A scenario without a
    // confirmed donor/commissioning map is diagnostic only and must not touch
    // ISD or enable the generator.
    if (!flag(node, "mapping_confirmed", false)) {
        return {RunVerdict::Incomplete,
            "ЯВП-8: карта коммутации ИСД не подтверждена; активное воздействие не выполнялось", {}};
    }
    if (!flag(node, "active_outputs_confirmed", false)) {
        return {RunVerdict::Incomplete,
            "ЯВП-8: активные воздействия не разрешены сценарием", {}};
    }

    const unsigned inputType = natural(node, "input_switch_type", 2);
    const unsigned gainType = natural(node, "gain_switch_type", 2);
    const unsigned measurementType = natural(node, "measurement_switch_type", 3);
    const unsigned measurementAnalogType = natural(node, "measurement_analog_type", 1);
    if (!inputType || !gainType || !measurementType || !measurementAnalogType)
        throw std::invalid_argument("ЯВП-8: некорректные типы коммутации ИСД");

    std::vector<std::vector<unsigned>> inputMap(channelCount);
    std::vector<std::vector<unsigned>> measurementMap(channelCount);
    std::vector<std::vector<unsigned>> channelGainContacts(channelCount);
    for (unsigned channel = 0; channel < channelCount; ++channel) {
        const std::string inputKey = "input_" + std::to_string(channel + 1) + "_contacts";
        const std::string measurementKey = "measurement_" + std::to_string(channel + 1) + "_contacts";
        const std::string gainContactsKey = "channel_" + std::to_string(channel + 1) + "_gain_contacts";
        if (!hasArgument(node, inputKey) || !hasArgument(node, measurementKey)
            || !hasArgument(node, gainContactsKey)) {
            return {RunVerdict::Incomplete,
                "ЯВП-8: карта входов, измерительных линий и КУ задана не для всех 8 каналов", {}};
        }
        inputMap[channel] = unsigneds(argument(node, inputKey));
        measurementMap[channel] = unsigneds(argument(node, measurementKey));
        channelGainContacts[channel] = unsigneds(argument(node, gainContactsKey));
        if (channelGainContacts[channel].size() != 4) {
            return {RunVerdict::Incomplete,
                "ЯВП-8: для каждого канала должны быть заданы четыре контакта КУ1..КУ4", {}};
        }
    }

    std::map<double, std::vector<unsigned>> gainBits;
    for (const double gain : gains) {
        const auto key = gainKey(gain) + "_bits";
        if (!hasArgument(node, key)) {
            return {RunVerdict::Incomplete,
                "ЯВП-8: отсутствует карта выбора коэффициента " + std::to_string(gain), {}};
        }
        auto bits = unsigneds(argument(node, key), true);
        std::set<unsigned> unique;
        for (const unsigned bit : bits) {
            if (bit < 1 || bit > 4 || !unique.insert(bit).second)
                throw std::invalid_argument("ЯВП-8: биты КУ должны быть уникальными 1..4");
        }
        gainBits[gain] = std::move(bits);
    }

    const double capacitancePf = number(node, "coupling_capacitance_pf", 1000.0);
    const unsigned settleMs = natural(node, "settle_ms", 2000);
    const unsigned v7ReadRetries = natural(node, "v7_read_retries", 2);
    const unsigned v7RetryDelayMs = natural(node, "v7_retry_delay_ms", 250);
    const double referenceFrequency = number(node, "reference_frequency_hz", 500.0);
    const double afcGain = number(node, "afc_gain_mv_per_pcl", 1.0);
    const double gainTolerance = number(node, "gain_tolerance_percent", 7.0);
    const double attenuationMinimum = number(node, "attenuation_min_db", 20.0);
    if (!(capacitancePf > 0.0) || !(referenceFrequency > 0.0)
        || !(gainTolerance > 0.0) || !(attenuationMinimum > 0.0)) {
        throw std::invalid_argument("ЯВП-8: некорректные параметры методики");
    }
    if (std::find(gains.begin(), gains.end(), afcGain) == gains.end())
        throw std::invalid_argument("ЯВП-8: afc_gain_mv_per_pcl отсутствует в ряду коэффициентов");
    if (std::find(frequencies.begin(), frequencies.end(), referenceFrequency) == frequencies.end())
        throw std::invalid_argument("ЯВП-8: reference_frequency_hz отсутствует в frequencies_hz");

    const std::string owner = "run:" + context.runId + ":yvp";
    const auto switchContact = [&](unsigned type, unsigned channel, bool enabled) {
        context.equipment.invoke("stand.switch_matrix", "switch", {
            {"type", std::to_string(type)},
            {"channel", std::to_string(channel)},
            {"enabled", enabled ? "true" : "false"},
            {"owner", owner}});
    };
    const auto analogOff = [&](unsigned channel) {
        context.equipment.invoke("stand.switch_matrix", "analog", {
            {"channel", std::to_string(channel)}, {"code", "0"},
            {"enabled", "false"}, {"owner", owner}});
    };
    const auto generatorOff = [&] {
        context.equipment.invoke("signal.generator", "output", {
            {"channel", "1"}, {"enabled", "false"}});
    };
    const auto cleanup = [&] {
        try { generatorOff(); } catch (...) {}
        try {
            context.equipment.invoke("stand.switch_matrix", "release_owner", {{"owner", owner}});
        } catch (...) {}
    };
    const auto readAcVoltage = [&](unsigned channel, double gain, double frequency) {
        for (unsigned attempt = 0;; ++attempt) {
            try {
                return responseNumber(context.equipment.invoke(
                    "measure.reference_ac_voltage", "read_ac_voltage", {}), "volts");
            } catch (const std::exception& error) {
                if (attempt >= v7ReadRetries) throw;
                publish(context, node, "V7_RETRY",
                    "Ошибка чтения В7; VISA-сеанс переподключается без смены воздействия",
                    RunVerdict::NotRun,
                    {{"channel", std::to_string(channel)},
                     {"gain_mv_per_pcl", std::to_string(gain)},
                     {"frequency_hz", std::to_string(frequency)},
                     {"attempt", std::to_string(attempt + 1)},
                     {"error", error.what()}});
                context.equipment.invoke(
                    "measure.reference_ac_voltage", "reconnect", {});
                waitChecked(context, v7RetryDelayMs);
            }
        }
    };

    ProcedureResult result{RunVerdict::Ok,
        "ЯВП-8 проверен по стендовому тракту Rigol / ИСД / В7", {}};
    std::size_t completedPoints = 0;
    const std::size_t pointCount = channelCount * (gains.size() - 1 + frequencies.size());

    try {
        // Start from the state owned by this run only. No firmware type=4 is
        // issued here: generic lifecycle and failures use targeted ownership.
        context.equipment.invoke("stand.switch_matrix", "release_owner", {{"owner", owner}});
        generatorOff();

        for (unsigned channel = 0; channel < channelCount; ++channel) {
            if (context.stopRequested.load()) throw std::runtime_error("Остановлено оператором");

            for (const unsigned contact : inputMap[channel])
                switchContact(inputType, contact, true);
            for (const unsigned contact : measurementMap[channel]) {
                analogOff(contact);
                switchContact(measurementType, contact, true);
            }

            for (const double gain : gains) {
                if (context.stopRequested.load()) throw std::runtime_error("Остановлено оператором");

                // Remove only this run's gain-selection contacts before the
                // next combination, leaving the input and measurement route up.
                for (const unsigned contact : channelGainContacts[channel])
                    switchContact(gainType, contact, false);
                for (const unsigned bit : gainBits.at(gain))
                    switchContact(gainType, channelGainContacts[channel].at(bit - 1), true);

                const double inputVpp = number(
                    node, gainKey(gain) + "_input_vpp", 2.0 / gain);
                if (!(inputVpp > 0.0))
                    throw std::invalid_argument("ЯВП-8: входная амплитуда должна быть положительной");
                const double chargePc = capacitancePf * inputVpp;

                const std::vector<double> gainFrequencies = std::abs(gain - afcGain) < 1e-9
                    ? frequencies : std::vector<double>{referenceFrequency};
                std::map<double, Observation> observations;

                for (const double frequency : gainFrequencies) {
                    context.equipment.invoke("signal.generator", "set_sine", {
                        {"channel", "1"},
                        {"frequency_hz", std::to_string(frequency)},
                        {"amplitude_vpp", std::to_string(inputVpp)},
                        {"offset_v", "0"}});
                    context.equipment.invoke("signal.generator", "output", {
                        {"channel", "1"}, {"enabled", "true"}});
                    waitChecked(context, settleMs);

                    const double measuredRms = readAcVoltage(channel + 1, gain, frequency);
                    std::string measuredFrequency;
                    if (frequency >= 10.0) {
                        try {
                            measuredFrequency = std::to_string(responseNumber(context.equipment.invoke(
                                "measure.reference_frequency", "read_frequency", {}), "hertz"));
                        } catch (...) {
                            // Frequency readback is diagnostic. The programmed
                            // Rigol frequency remains part of the run evidence.
                        }
                    }
                    const double outputVpp = measuredRms * 2.0 * std::sqrt(2.0);
                    const double calculatedGain = 1000.0 * outputVpp / chargePc;
                    observations[frequency] = {calculatedGain, measuredRms, outputVpp, inputVpp};
                    ++completedPoints;

                    publish(context, node, "YVP_POINT",
                        "ЯВП " + std::to_string(channel + 1)
                            + " · K=" + std::to_string(gain)
                            + " · " + std::to_string(frequency) + " Гц",
                        RunVerdict::NotRun,
                        {{"channel", std::to_string(channel + 1)},
                         {"gain_mv_per_pcl", std::to_string(gain)},
                         {"set_frequency_hz", std::to_string(frequency)},
                         {"measured_frequency_hz", measuredFrequency},
                         {"input_vpp", std::to_string(inputVpp)},
                         {"v7_vrms", std::to_string(measuredRms)},
                         {"output_vpp", std::to_string(outputVpp)},
                         {"calculated_gain_mv_per_pcl", std::to_string(calculatedGain)},
                         {"point_index", std::to_string(completedPoints)},
                         {"point_count", std::to_string(pointCount)}});
                    generatorOff();
                }

                const auto reference = observations.find(referenceFrequency);
                if (reference == observations.end())
                    throw std::runtime_error("ЯВП-8: не снята опорная точка 500 Гц");

                auto gainResult = measurement(
                    "ubsi.yvp.gain." + std::to_string(channel + 1) + "." + gainKey(gain),
                    "ЯВП " + std::to_string(channel + 1)
                        + ": коэффициент " + std::to_string(gain) + " мВ/пКл",
                    gain, reference->second.calculatedGain,
                    gain * (1.0 - gainTolerance / 100.0),
                    gain * (1.0 + gainTolerance / 100.0), "мВ/пКл");
                gainResult.attributes = {
                    {"channel", std::to_string(channel + 1)},
                    {"reference_frequency_hz", std::to_string(referenceFrequency)},
                    {"input_vpp", std::to_string(reference->second.inputVpp)},
                    {"v7_vrms", std::to_string(reference->second.measuredRms)},
                    {"output_vpp", std::to_string(reference->second.outputVpp)},
                    {"error_percent", std::to_string(relativeError(
                        reference->second.calculatedGain, gain))},
                    {"backend", "isd_v7"}};
                append(result, std::move(gainResult));

                if (std::abs(gain - afcGain) > 1e-9) continue;

                const std::vector<std::pair<double, double>> afcLimits{
                    {2.0, 10.0}, {6.0, 5.0}, {20.0, 5.0},
                    {1800.0, 5.0}, {2000.0, 10.0}};
                for (const auto& [frequency, tolerance] : afcLimits) {
                    const auto point = observations.find(frequency);
                    if (point == observations.end())
                        throw std::invalid_argument("ЯВП-8: отсутствует обязательная точка АЧХ "
                            + std::to_string(frequency) + " Гц");
                    const double deviation = relativeError(
                        point->second.calculatedGain, reference->second.calculatedGain);
                    auto afc = measurement(
                        "ubsi.yvp.afc." + std::to_string(channel + 1)
                            + "." + std::to_string(static_cast<unsigned>(frequency)),
                        "ЯВП " + std::to_string(channel + 1)
                            + ": АЧХ " + std::to_string(frequency) + " Гц",
                        0.0, deviation, -tolerance, tolerance, "%");
                    afc.attributes = {
                        {"channel", std::to_string(channel + 1)},
                        {"gain_mv_per_pcl", std::to_string(gain)},
                        {"reference_frequency_hz", std::to_string(referenceFrequency)},
                        {"set_frequency_hz", std::to_string(frequency)},
                        {"v7_vrms", std::to_string(point->second.measuredRms)},
                        {"backend", "isd_v7"}};
                    append(result, std::move(afc));
                }

                const auto high = observations.find(4000.0);
                if (high == observations.end())
                    throw std::invalid_argument("ЯВП-8: отсутствует обязательная точка 4000 Гц");
                const double attenuation = attenuationDb(
                    reference->second.calculatedGain, high->second.calculatedGain);
                auto attenuationResult = measurement(
                    "ubsi.yvp.attenuation." + std::to_string(channel + 1),
                    "ЯВП " + std::to_string(channel + 1)
                        + ": затухание 4000 Гц относительно 500 Гц",
                    attenuationMinimum, attenuation,
                    attenuationMinimum, 1.0e9, "дБ");
                attenuationResult.attributes = {
                    {"channel", std::to_string(channel + 1)},
                    {"reference_frequency_hz", std::to_string(referenceFrequency)},
                    {"set_frequency_hz", "4000"},
                    {"v7_vrms", std::to_string(high->second.measuredRms)},
                    {"backend", "isd_v7"}};
                append(result, std::move(attenuationResult));
            }

            // End the channel at a physical boundary; targeted release ensures
            // a failed transition cannot leave an input/measurement route on.
            cleanup();
        }
    } catch (const std::exception& error) {
        cleanup();
        return {RunVerdict::Error,
            "Ошибка стенда ЯВП после " + std::to_string(completedPoints)
                + " из " + std::to_string(pointCount) + " точек: " + error.what(),
            std::move(result.measurements)};
    } catch (...) {
        cleanup();
        return {RunVerdict::Error,
            "Неизвестная ошибка стенда ЯВП после " + std::to_string(completedPoints)
                + " из " + std::to_string(pointCount) + " точек",
            std::move(result.measurements)};
    }

    cleanup();
    if (result.verdict == RunVerdict::Fail)
        result.message = "ЯВП-8: имеются измерения вне допуска";
    return result;
}

} // namespace

void registerV7UbsiProcedures(ScenarioEngine& engine)
{
    engine.registerProcedure("yvp.v7", yvpV7Isd);
    engine.registerProcedure("ubsi.yvp", yvpV7Isd);
}

} // namespace orbita::stand
