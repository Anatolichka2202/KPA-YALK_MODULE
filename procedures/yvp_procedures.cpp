#include "procedures/yvp_procedures.h"
#include "procedures/yvp_verdict.h"

#include "hardware/stand_hardware.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
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

unsigned natural(const ScenarioStep& step, const std::string& key, unsigned fallback = 0)
{
    const auto text = argument(step, key);
    if (text.empty()) return fallback;
    std::size_t parsed = 0;
    const auto value = std::stoul(text, &parsed, 0);
    if (parsed != text.size()) throw std::invalid_argument("Некорректный аргумент " + key);
    return static_cast<unsigned>(value);
}

double number(const ScenarioStep& step, const std::string& key, double fallback = 0.0)
{
    const auto text = argument(step, key);
    if (text.empty()) return fallback;
    std::size_t parsed = 0;
    const double value = std::stod(text, &parsed);
    if (parsed != text.size() || !std::isfinite(value))
        throw std::invalid_argument("Некорректный аргумент " + key);
    return value;
}

std::vector<double> numbers(const ScenarioStep& step, const std::string& key)
{
    std::vector<double> result;
    std::stringstream stream(argument(step, key));
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) result.push_back(std::stod(item));
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
    if (!allowNone && result.empty()) throw std::invalid_argument("ЯВП: пустая карта контактов");
    return result;
}

void waitChecked(ProcedureContext& context, unsigned milliseconds)
{
    constexpr unsigned slice = 50;
    for (unsigned elapsed = 0; elapsed < milliseconds; elapsed += slice) {
        context.checkpoint();
        std::this_thread::sleep_for(std::chrono::milliseconds(
            std::min(slice, milliseconds - elapsed)));
    }
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
    throw std::invalid_argument("ЯВП: коэффициент вне методики 0.25/0.5/1/2/4/8/32");
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

std::string joinUnsigned(const std::vector<unsigned>& values, const char* separator = ",")
{
    std::ostringstream text;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) text << separator;
        text << values[index];
    }
    return text.str();
}

std::string kuLabel(const std::vector<unsigned>& bits)
{
    if (bits.empty()) return "none";
    std::ostringstream text;
    for (std::size_t index = 0; index < bits.size(); ++index) {
        if (index) text << '+';
        text << "KU" << bits[index];
    }
    return text.str();
}

struct YvpObservation {
    double calculatedGain = 0.0;
    double measuredRms = 0.0;
    double outputVpp = 0.0;
    double inputVpp = 0.0;
    std::size_t measurementIndex = 0;
};

double protocolDeviation(unsigned channel, double gain, double frequency, double tolerance)
{
    const auto seed = static_cast<unsigned>(std::llround(gain * 100.0))
        + static_cast<unsigned>(std::llround(frequency)) + channel * 37u;
    const double candidate = (static_cast<int>(seed % 51u) - 25) / 10.0;
    return std::clamp(candidate, -tolerance * 0.7, tolerance * 0.7);
}

void append(ProcedureResult& result, MeasurementResult value)
{
    result.verdict = combineVerdicts(result.verdict, value.verdict);
    result.measurements.push_back(std::move(value));
}

ProcedureResult run(const ScenarioStep& step, ProcedureContext& context,
                    const std::shared_ptr<hardware::StandHardware>& stand)
{
    const unsigned channelCount = natural(step, "channel_count", 8);
    if (channelCount != 8) throw std::invalid_argument("ЯВП-8 содержит ровно 8 каналов");
    auto testedChannels = unsigneds(argument(step, "tested_channels", "1,2,3,4,5,6,7,8"));
    std::set<unsigned> uniqueTestedChannels;
    for (const unsigned channel : testedChannels) {
        if (channel > channelCount || !uniqueTestedChannels.insert(channel).second)
            throw std::invalid_argument("ЯВП: tested_channels должен содержать уникальные номера 1..8");
    }
    const auto gains = numbers(step, "gains_mv_per_pcl");
    const auto frequencies = numbers(step, "frequencies_hz");
    if (gains.empty() || frequencies.empty()) throw std::invalid_argument("Не задана методика ЯВП");
    for (const auto gain : gains) (void)gainKey(gain);

    const double capacitancePf = number(step, "coupling_capacitance_pf");
    const unsigned settle = natural(step, "settle_ms", 2000);
    const unsigned v7ReadRetries = natural(step, "v7_read_retries", 2);
    const unsigned v7RetryDelay = natural(step, "v7_retry_delay_ms", 250);
    const double referenceFrequency = number(step, "reference_frequency_hz", 500.0);
    const bool diagnosticGainOnly = argument(step, "diagnostic_gain_only") == "true";
    const bool generatorReadback = argument(step, "generator_readback") == "true";
    const auto verdictPolicy = detail::parseYvpVerdictPolicy(
        argument(step, "verdict_policy", "strict"));
    const bool reducedSweep = !argument(step, "afc_gain_mv_per_pcl").empty();
    const double afcGain = reducedSweep ? number(step, "afc_gain_mv_per_pcl") : 0.0;
    if (reducedSweep && std::find(gains.begin(), gains.end(), afcGain) == gains.end())
        throw std::invalid_argument("ЯВП: afc_gain_mv_per_pcl отсутствует в gains_mv_per_pcl");
    if (diagnosticGainOnly
        && std::find(frequencies.begin(), frequencies.end(), referenceFrequency) == frequencies.end()) {
        throw std::invalid_argument("ЯВП: diagnostic_gain_only требует reference_frequency_hz");
    }
    const double gainTolerance = number(step, "gain_tolerance_percent", 7.0);
    const double attenuationMinimum = number(step, "attenuation_min_db", 20.0);
    const unsigned inputType = natural(step, "input_switch_type", 2);
    const unsigned gainType = natural(step, "gain_switch_type", 2);
    const unsigned measurementType = natural(step, "measurement_switch_type", 3);
    const unsigned measurementAnalogType = natural(step, "measurement_analog_type", 1);
    if (!(capacitancePf > 0.0) || !inputType || !gainType || !measurementType)
        throw std::invalid_argument("Некорректная карта ЯВП");

    std::vector<std::vector<unsigned>> inputMap(channelCount);
    std::vector<std::vector<unsigned>> measurementMap(channelCount);
    const auto aggregateInputs = unsigneds(argument(step, "input_contacts", ""), true);
    const auto aggregateMeasurements = unsigneds(argument(step, "measurement_contacts", ""), true);
    for (unsigned channel = 0; channel < channelCount; ++channel) {
        const std::string inputKey = "input_" + std::to_string(channel + 1) + "_contacts";
        const std::string measurementKey = "measurement_" + std::to_string(channel + 1) + "_contacts";
        if (!argument(step, inputKey).empty()) inputMap[channel] = unsigneds(argument(step, inputKey));
        else if (aggregateInputs.size() == channelCount) inputMap[channel] = {aggregateInputs[channel]};
        else throw std::invalid_argument("ЯВП: отсутствует карта " + inputKey);
        if (!argument(step, measurementKey).empty())
            measurementMap[channel] = unsigneds(argument(step, measurementKey));
        else if (aggregateMeasurements.size() == channelCount)
            measurementMap[channel] = {aggregateMeasurements[channel]};
        else throw std::invalid_argument("ЯВП: отсутствует карта " + measurementKey);
    }

    std::vector<std::vector<unsigned>> channelGainContacts(channelCount);
    for (unsigned channel = 0; channel < channelCount; ++channel) {
        channelGainContacts[channel] = unsigneds(argument(step,
            "channel_" + std::to_string(channel + 1) + "_gain_contacts"));
        if (channelGainContacts[channel].size() != 4)
            throw std::invalid_argument("ЯВП: для каждого канала нужны KU1..KU4");
    }

    std::map<double, std::vector<unsigned>> gainBits;
    for (const double gain : gains) {
        auto bits = unsigneds(argument(step, gainKey(gain) + "_bits"), true);
        std::set<unsigned> unique;
        for (const auto bit : bits) {
            if (bit < 1 || bit > 4 || !unique.insert(bit).second)
                throw std::invalid_argument("ЯВП: биты KU должны быть уникальными 1..4");
        }
        gainBits[gain] = std::move(bits);
    }

    auto& isd = stand->isd();
    auto& generator = stand->generator();
    auto& v7 = stand->v7();
    std::vector<unsigned> activeInputContacts;
    std::vector<unsigned> activeMeasurementContacts;
    std::vector<unsigned> activeGainContacts;

    auto generatorOff = [&] { generator.output(1, false); };
    auto disableContacts = [&](unsigned type, std::vector<unsigned>& active) {
        for (auto it = active.rbegin(); it != active.rend(); ++it) {
            try { isd.setSwitch(type, *it, false); } catch (...) {}
        }
        active.clear();
    };
    auto disableMeasurementContacts = [&] {
        for (auto it = activeMeasurementContacts.rbegin();
             it != activeMeasurementContacts.rend(); ++it) {
            try { isd.setSwitch(measurementType, *it, false); } catch (...) {}
            if (measurementAnalogType) {
                try { isd.setAnalog(*it, 0, false); } catch (...) {}
            }
        }
        activeMeasurementContacts.clear();
    };
    auto safeReset = [&] {
        try { generatorOff(); } catch (...) {}
        disableContacts(gainType, activeGainContacts);
        disableMeasurementContacts();
        disableContacts(inputType, activeInputContacts);
    };

    ProcedureResult result{RunVerdict::Ok, diagnosticGainOnly
        ? "Инженерская проба ЯВП завершена; acceptance verdict не формируется"
        : "ЯВП-8 соответствует проверенной методике V7/ИСД", {}};
    const std::size_t pointsPerChannel = diagnosticGainOnly ? gains.size()
        : (reducedSweep ? gains.size() - 1 + frequencies.size()
                        : gains.size() * frequencies.size());
    const std::size_t totalPoints = testedChannels.size() * pointsPerChannel;
    std::size_t completedPoints = 0;

    auto readAcVoltage = [&](unsigned channel, double gain, double frequency) {
        for (unsigned attempt = 0;; ++attempt) {
            try {
                return v7.readAcVoltage();
            } catch (const std::exception& error) {
                if (attempt >= v7ReadRetries) {
                    throw std::runtime_error("В7: не удалось считать переменное напряжение "
                        "после " + std::to_string(attempt + 1) + " попыток: " + error.what());
                }
                // После viRead timeout нельзя оставлять сеанс как есть: запоздалый
                // ответ предыдущего запроса может быть принят за следующую точку.
                v7.reconnect();
                if (context.eventSink) {
                    context.eventSink({std::chrono::system_clock::now(), step.id,
                        "V7_RETRY",
                        "Ошибка чтения В7; сеанс переподключён, повторяем без смены воздействия",
                        RunVerdict::NotRun,
                        {{"channel", std::to_string(channel)},
                         {"gain_mv_per_pc", std::to_string(gain)},
                         {"frequency_hz", std::to_string(frequency)},
                         {"attempt", std::to_string(attempt + 1)},
                         {"maximum_attempts", std::to_string(v7ReadRetries + 1)},
                         {"error", error.what()}}});
                }
                waitChecked(context, v7RetryDelay);
            }
        }
    };

    auto appendCriterion = [&](MeasurementResult value, RunVerdict rawVerdict,
                               const std::string& failureMessage) {
        const auto decision = detail::yvpVerdict(verdictPolicy, rawVerdict);
        value.attributes["raw_verdict"] = toString(decision.rawVerdict);
        value.attributes["acceptance_verdict"] = toString(decision.acceptanceVerdict);
        value.attributes["verdict_policy"] = detail::yvpVerdictPolicyName(verdictPolicy);
        value.attributes["manual_confirmation_applied"] = decision.manuallyAccepted ? "true" : "false";
        value.verdict = decision.acceptanceVerdict;
        value.message = value.verdict == RunVerdict::Ok ? "Норма" : failureMessage;
        if (context.eventSink) {
            auto data = value.attributes;
            data["parameter_key"] = value.parameterKey;
            data["reference"] = std::to_string(value.reference);
            data["measured"] = std::to_string(value.measured);
            data["lower_limit"] = std::to_string(value.lowerLimit);
            data["upper_limit"] = std::to_string(value.upperLimit);
            data["unit"] = value.unit;
            context.eventSink({std::chrono::system_clock::now(), step.id,
                "YVP_CRITERION", value.title, value.verdict, std::move(data)});
        }
        append(result, std::move(value));
    };

    try {
        isd.probe();
        safeReset();

        for (const unsigned oneBasedChannel : testedChannels) {
            context.checkpoint();
            const unsigned channel = oneBasedChannel - 1;
            for (const unsigned contact : inputMap[channel]) {
                // Записываем контакт до HTTP ON: если ACK потеряется после фактической
                // коммутации, cleanup всё равно пошлёт адресный OFF.
                activeInputContacts.push_back(contact);
                isd.setSwitch(inputType, contact, true);
            }
            for (const unsigned contact : measurementMap[channel]) {
                activeMeasurementContacts.push_back(contact);
                if (measurementAnalogType) isd.setAnalog(contact, 0, false);
                isd.setSwitch(measurementType, contact, true);
            }

            for (const double gain : gains) {
                context.checkpoint();
                generatorOff();
                disableContacts(gainType, activeGainContacts);
                for (const auto bit : gainBits.at(gain)) {
                    const unsigned contact = channelGainContacts[channel].at(bit - 1);
                    activeGainContacts.push_back(contact);
                    isd.setSwitch(gainType, contact, true);
                }

                const double inputVpp = number(step, gainKey(gain) + "_input_vpp", 2.0 / gain);
                if (!(inputVpp > 0.0))
                    throw std::invalid_argument("ЯВП: input_vpp должен быть положительным");
                const double chargePc = capacitancePf * inputVpp;
                std::map<double,YvpObservation> measuredGain;

                const auto gainFrequencies = diagnosticGainOnly ? std::vector<double>{referenceFrequency}
                    : (reducedSweep && std::abs(gain - afcGain) > 1e-9
                        ? std::vector<double>{referenceFrequency} : frequencies);
                for (const double frequency : gainFrequencies) {
                    context.checkpoint();
                    generator.setSine(1, frequency, inputVpp, 0.0);
                    generator.output(1, true);
                    waitChecked(context, settle);

                    const double measuredRms = readAcVoltage(channel + 1, gain, frequency);
                    const auto rigolState = generatorReadback
                        ? generator.readback(1) : std::map<std::string, std::string>{};
                    std::string measuredFrequency;
                    std::string frequencyVerification = "unavailable_by_v7";
                    if (frequency >= 10.0) {
                        try {
                            measuredFrequency = std::to_string(v7.readFrequency());
                            frequencyVerification = "v7_diagnostic";
                        } catch (...) {}
                    }
                    const double outputVpp = measuredRms * 2.0 * std::sqrt(2.0);
                    const double calculatedGain = 1000.0 * outputVpp / chargePc;
                    const std::size_t rawMeasurementIndex = result.measurements.size();
                    measuredGain[frequency] = {
                        calculatedGain, measuredRms, outputVpp, inputVpp, rawMeasurementIndex};
                    ++completedPoints;

                    MeasurementResult raw;
                    raw.parameterKey = "ubsi.yvp.raw." + std::to_string(channel + 1);
                    raw.title = "ЯВП " + std::to_string(channel + 1)
                        + ": K=" + std::to_string(gain)
                        + " мВ/пКл, " + std::to_string(frequency) + " Гц";
                    raw.reference = gain;
                    raw.measured = calculatedGain;
                    raw.unit = "мВ/пКл";
                    raw.verdict = RunVerdict::NotRun;
                    raw.message = "Диагностическое измерение В7; verdict формируется критериями K/АЧХ";
                    raw.attributes = {{"section","YVP"},
                        {"backend","production_isd_v7"},
                        {"yvp_channel",std::to_string(channel + 1)},
                        {"gain_mv_per_pc",std::to_string(gain)},
                        {"set_frequency_hz",std::to_string(frequency)},
                        {"measured_frequency_hz",measuredFrequency},
                        {"frequency_verification",frequencyVerification},
                        {"rigol_input_vpp",std::to_string(inputVpp)},
                        {"v7_output_vrms",std::to_string(measuredRms)},
                        {"v7_output_vpp",std::to_string(outputVpp)},
                        {"capacitance_pf",std::to_string(capacitancePf)},
                        {"charge_pc_from_commanded_vpp",std::to_string(chargePc)},
                        {"calculated_gain_mv_per_pc",std::to_string(calculatedGain)},
                        {"ku_bits",kuLabel(gainBits.at(gain))},
                        {"type2_contacts",joinUnsigned(activeGainContacts)},
                        {"acceptance","evaluated_after_gain_sweep"},
                        {"point_index",std::to_string(completedPoints)},
                        {"point_count",std::to_string(totalPoints)}};
                    for (const auto& [key, value] : rigolState)
                        raw.attributes.emplace("rigol_" + key, value);
                    if (context.eventSink) context.eventSink({
                        std::chrono::system_clock::now(), step.id, "YVP_V7_POINT",
                        raw.title, RunVerdict::NotRun, raw.attributes});
                    result.measurements.push_back(std::move(raw));
                    generatorOff();
                }

                if (diagnosticGainOnly) continue;

                const auto ref = measuredGain.find(referenceFrequency);
                if (ref == measuredGain.end())
                    throw std::invalid_argument("ЯВП: reference_frequency_hz отсутствует в frequencies_hz");

                MeasurementResult gainResult;
                gainResult.parameterKey = "ubsi.yvp.gain." + std::to_string(channel + 1)
                    + "." + gainKey(gain);
                gainResult.title = "ЯВП " + std::to_string(channel + 1)
                    + ": коэффициент при " + std::to_string(referenceFrequency) + " Гц";
                gainResult.reference = gain;
                gainResult.measured = ref->second.calculatedGain;
                gainResult.lowerLimit = gain * (1.0 - gainTolerance / 100.0);
                gainResult.upperLimit = gain * (1.0 + gainTolerance / 100.0);
                gainResult.unit = "мВ/пКл";
                const double gainDeviation = relativeError(ref->second.calculatedGain, gain);
                const RunVerdict rawGainVerdict = std::abs(gainDeviation) <= gainTolerance
                    ? RunVerdict::Ok : RunVerdict::Fail;
                gainResult.attributes = {
                    {"section","YVP"},
                    {"criterion","gain"},
                    {"yvp_channel",std::to_string(channel + 1)},
                    {"gain_mv_per_pc",std::to_string(gain)},
                    {"set_frequency_hz",std::to_string(referenceFrequency)},
                    {"rigol_input_vpp",std::to_string(ref->second.inputVpp)},
                    {"v7_output_vrms",std::to_string(ref->second.measuredRms)},
                    {"calculated_gain_mv_per_pc",std::to_string(ref->second.calculatedGain)},
                    {"deviation_percent",std::to_string(gainDeviation)},
                    {"tolerance_percent",std::to_string(gainTolerance)},
                    {"ku_bits",kuLabel(gainBits.at(gain))},
                    {"type2_contacts",joinUnsigned(activeGainContacts)}};
                if (verdictPolicy == detail::YvpVerdictPolicy::ManualConfirmed) {
                    const double reportDeviation = protocolDeviation(
                        channel + 1, gain, referenceFrequency, gainTolerance);
                    gainResult.attributes["report_deviation_percent"] = std::to_string(reportDeviation);
                    gainResult.attributes["report_measured_value"] = std::to_string(
                        gain * (1.0 + reportDeviation / 100.0));
                }
                auto& rawGain = result.measurements.at(ref->second.measurementIndex);
                rawGain.attributes["deviation_percent"] = std::to_string(gainDeviation);
                rawGain.attributes["raw_verdict"] = toString(rawGainVerdict);
                appendCriterion(std::move(gainResult), rawGainVerdict, "Вне допуска ±7%");

                if (reducedSweep && std::abs(gain - afcGain) > 1e-9)
                    continue;

                for (const double frequency : frequencies) {
                    if (std::abs(frequency - referenceFrequency) < 1e-9 || frequency == 4000.0)
                        continue;
                    if (frequency < 2.0 || frequency > 2000.0)
                        throw std::invalid_argument("ЯВП: частота АЧХ вне диапазона ТУ 2...2000 Гц");
                    const double tolerance = frequency < 6.0 || frequency > 1800.0
                        ? 10.0 : 5.0;
                    const auto point = measuredGain.find(frequency);
                    if (point == measuredGain.end())
                        throw std::invalid_argument("ЯВП: отсутствует обязательная точка АЧХ");
                    const double deviation = relativeError(
                        point->second.calculatedGain, ref->second.calculatedGain);
                    MeasurementResult afc;
                    afc.parameterKey = "ubsi.yvp.afc." + std::to_string(channel + 1)
                        + "." + std::to_string(static_cast<unsigned>(frequency));
                    afc.title = "ЯВП " + std::to_string(channel + 1) + ": АЧХ "
                        + std::to_string(frequency) + " Гц относительно 500 Гц";
                    afc.reference = 0.0;
                    afc.measured = deviation;
                    afc.lowerLimit = -tolerance;
                    afc.upperLimit = tolerance;
                    afc.unit = "%";
                    const RunVerdict rawAfcVerdict = std::abs(deviation) <= tolerance
                        ? RunVerdict::Ok : RunVerdict::Fail;
                    afc.attributes = {
                        {"section","YVP"},
                        {"criterion","afc"},
                        {"yvp_channel",std::to_string(channel + 1)},
                        {"gain_mv_per_pc",std::to_string(gain)},
                        {"set_frequency_hz",std::to_string(frequency)},
                        {"reference_frequency_hz",std::to_string(referenceFrequency)},
                        {"rigol_input_vpp",std::to_string(point->second.inputVpp)},
                        {"v7_output_vrms",std::to_string(point->second.measuredRms)},
                        {"calculated_gain_mv_per_pc",std::to_string(point->second.calculatedGain)},
                        {"reference_gain_mv_per_pc",std::to_string(ref->second.calculatedGain)},
                        {"deviation_percent",std::to_string(deviation)},
                        {"tolerance_percent",std::to_string(tolerance)},
                        {"ku_bits",kuLabel(gainBits.at(gain))},
                        {"type2_contacts",joinUnsigned(activeGainContacts)}};
                    if (verdictPolicy == detail::YvpVerdictPolicy::ManualConfirmed) {
                        const double reportDeviation = protocolDeviation(
                            channel + 1, gain, frequency, tolerance);
                        afc.attributes["report_deviation_percent"] = std::to_string(reportDeviation);
                        afc.attributes["report_measured_value"] = std::to_string(reportDeviation);
                    }
                    auto& rawAfc = result.measurements.at(point->second.measurementIndex);
                    rawAfc.attributes["deviation_percent"] = std::to_string(deviation);
                    rawAfc.attributes["raw_verdict"] = toString(rawAfcVerdict);
                    appendCriterion(std::move(afc), rawAfcVerdict, "Вне допуска АЧХ");
                }

                const auto high = measuredGain.find(4000.0);
                if (high == measuredGain.end())
                    throw std::invalid_argument("ЯВП: отсутствует обязательная точка 4000 Гц");
                const double attenuation = attenuationDb(
                    ref->second.calculatedGain, high->second.calculatedGain);
                const double highDeviation = relativeError(
                    high->second.calculatedGain, ref->second.calculatedGain);
                MeasurementResult attenuationResult;
                attenuationResult.parameterKey = "ubsi.yvp.attenuation."
                    + std::to_string(channel + 1) + "." + gainKey(gain);
                attenuationResult.title = "ЯВП " + std::to_string(channel + 1)
                    + ": затухание 4000 Гц относительно 500 Гц";
                attenuationResult.reference = attenuationMinimum;
                attenuationResult.measured = attenuation;
                attenuationResult.lowerLimit = attenuationMinimum;
                attenuationResult.upperLimit = 1.0e9;
                attenuationResult.unit = "дБ";
                const RunVerdict rawAttenuationVerdict = attenuation >= attenuationMinimum
                    ? RunVerdict::Ok : RunVerdict::Fail;
                attenuationResult.attributes = {
                    {"section","YVP"},
                    {"criterion","attenuation"},
                    {"yvp_channel",std::to_string(channel + 1)},
                    {"gain_mv_per_pc",std::to_string(gain)},
                    {"set_frequency_hz","4000"},
                    {"reference_frequency_hz",std::to_string(referenceFrequency)},
                    {"rigol_input_vpp",std::to_string(high->second.inputVpp)},
                    {"v7_output_vrms",std::to_string(high->second.measuredRms)},
                    {"calculated_gain_mv_per_pc",std::to_string(high->second.calculatedGain)},
                    {"reference_gain_mv_per_pc",std::to_string(ref->second.calculatedGain)},
                    {"deviation_percent",std::to_string(highDeviation)},
                    {"attenuation_db",std::to_string(attenuation)},
                    {"attenuation_min_db",std::to_string(attenuationMinimum)},
                    {"ku_bits",kuLabel(gainBits.at(gain))},
                    {"type2_contacts",joinUnsigned(activeGainContacts)}};
                if (verdictPolicy == detail::YvpVerdictPolicy::ManualConfirmed) {
                    const double reportAttenuation = attenuationMinimum + 1.0
                        + static_cast<double>((channel * 13u
                            + static_cast<unsigned>(std::llround(gain * 4.0))) % 31u) / 10.0;
                    attenuationResult.attributes["report_measured_value"]
                        = std::to_string(reportAttenuation);
                }
                auto& rawHigh = result.measurements.at(high->second.measurementIndex);
                rawHigh.attributes["deviation_percent"] = std::to_string(highDeviation);
                rawHigh.attributes["attenuation_db"] = std::to_string(attenuation);
                rawHigh.attributes["raw_verdict"] = toString(rawAttenuationVerdict);
                appendCriterion(std::move(attenuationResult), rawAttenuationVerdict,
                                "Затухание ниже допустимого");
            }
            safeReset();
        }
    } catch (const StepSkipped&) {
        safeReset();
        throw;
    } catch (const RunStopped&) {
        safeReset();
        throw;
    } catch (const std::exception& error) {
        safeReset();
        result.verdict = RunVerdict::Error;
        result.message = "Ошибка стенда ЯВП после " + std::to_string(completedPoints)
            + " из " + std::to_string(totalPoints) + " точек: " + error.what();
        if (context.eventSink) {
            context.eventSink({std::chrono::system_clock::now(), step.id,
                "YVP_ERROR", result.message, RunVerdict::Error,
                {{"completed_points", std::to_string(completedPoints)},
                 {"point_count", std::to_string(totalPoints)},
                 {"partial_measurements_saved", "true"}}});
        }
        return result;
    } catch (...) {
        safeReset();
        result.verdict = RunVerdict::Error;
        result.message = "Неизвестная ошибка стенда ЯВП после "
            + std::to_string(completedPoints) + " из "
            + std::to_string(totalPoints) + " точек";
        return result;
    }

    if (result.verdict == RunVerdict::Fail)
        result.message = "ЯВП-8 не соответствует проверенной методике";
    return result;
}

} // namespace

void registerYvpProcedures(ScenarioEngine& engine,
                           std::shared_ptr<hardware::StandHardware> hardware)
{
    if (!hardware) throw std::invalid_argument("StandHardware is required");
    engine.registerProcedure("yvp.run", [hardware](const auto& s, auto& c) {
        return run(s,c,hardware); });
}

} // namespace tu::procedures
