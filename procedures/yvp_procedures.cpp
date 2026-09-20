#include "procedures/yvp_procedures.h"

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
    while (std::getline(stream, item, ',')) if (!item.empty()) result.push_back(std::stod(item));
    return result;
}

std::vector<unsigned> unsigneds(const std::string& text, bool allowNone = false)
{
    if (allowNone && (text.empty() || text == "none")) return {};
    std::vector<unsigned> result;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) result.push_back(static_cast<unsigned>(std::stoul(item)));
    }
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
    const auto gains = numbers(step, "gains_mv_per_pcl");
    const auto frequencies = numbers(step, "frequencies_hz");
    if (gains.empty() || frequencies.empty()) throw std::invalid_argument("Не задана методика ЯВП");
    for (const auto gain : gains) (void)gainKey(gain);

    const double capacitancePf = number(step, "coupling_capacitance_pf");
    const unsigned settle = natural(step, "settle_ms", 2000);
    const double referenceFrequency = number(step, "reference_frequency_hz", 500.0);
    const double gainTolerance = number(step, "gain_tolerance_percent", 7.0);
    const double attenuationMinimum = number(step, "attenuation_min_db", 20.0);
    const unsigned inputType = natural(step, "input_switch_type", 2);
    const unsigned gainType = natural(step, "gain_switch_type", 2);
    const unsigned measurementType = natural(step, "measurement_switch_type", 3);
    const unsigned measurementAnalogType = natural(step, "measurement_analog_type", 1);
    if (!(capacitancePf > 0.0) || !inputType || !gainType || !measurementType)
        throw std::invalid_argument("Некорректная карта ЯВП");

    auto inputContacts = unsigneds(argument(step, "input_contacts"));
    auto measurementContacts = unsigneds(argument(step, "measurement_contacts"));
    if (inputContacts.size() != channelCount || measurementContacts.size() != channelCount)
        throw std::invalid_argument("ЯВП: input_contacts/measurement_contacts должны содержать 8 каналов");

    std::vector<std::vector<unsigned>> ku(channelCount);
    for (unsigned channel = 0; channel < channelCount; ++channel) {
        ku[channel] = unsigneds(argument(step,
            "channel_" + std::to_string(channel + 1) + "_gain_contacts"));
        if (ku[channel].size() != 4)
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
    std::vector<unsigned> activeGain;
    unsigned activeInput = 0;
    unsigned activeMeasurement = 0;

    auto generatorOff = [&] { generator.output(1, false); };
    auto clearGain = [&] {
        for (auto it = activeGain.rbegin(); it != activeGain.rend(); ++it) {
            try { isd.setSwitch(gainType, *it, false); } catch (...) {}
        }
        activeGain.clear();
    };
    auto clearMeasurement = [&] {
        if (!activeMeasurement) return;
        try { isd.setSwitch(measurementType, activeMeasurement, false); } catch (...) {}
        if (measurementAnalogType) {
            try { isd.setAnalog(activeMeasurement, 0, false); } catch (...) {}
        }
        activeMeasurement = 0;
    };
    auto clearInput = [&] {
        if (!activeInput) return;
        try { isd.setSwitch(inputType, activeInput, false); } catch (...) {}
        activeInput = 0;
    };
    auto safeReset = [&] {
        try { generatorOff(); } catch (...) {}
        clearGain();
        clearMeasurement();
        clearInput();
    };

    ProcedureResult result{RunVerdict::Ok, "ЯВП-8 соответствует проверенной методике V7/ИСД", {}};
    try {
        // Перед любым активным воздействием подтверждаем доступность ИСД.
        isd.probe();
        safeReset();

        for (unsigned channel = 0; channel < channelCount; ++channel) {
            context.checkpoint();
            isd.setSwitch(inputType, inputContacts[channel], true);
            activeInput = inputContacts[channel];
            if (measurementAnalogType)
                isd.setAnalog(measurementContacts[channel], 0, false);
            isd.setSwitch(measurementType, measurementContacts[channel], true);
            activeMeasurement = measurementContacts[channel];

            for (const double gain : gains) {
                context.checkpoint();
                generatorOff();
                clearGain();
                for (const auto bit : gainBits.at(gain)) {
                    const unsigned contact = ku[channel].at(bit - 1);
                    isd.setSwitch(gainType, contact, true);
                    activeGain.push_back(contact);
                }

                // Точно как в отлаженной процедуре: Uвх,pp = 2/K.
                const double inputVpp = 2.0 / gain;
                const double chargePc = capacitancePf * inputVpp;
                std::map<double,double> measuredGain;

                for (const double frequency : frequencies) {
                    context.checkpoint();
                    generator.setSine(1, frequency, inputVpp, 0.0);
                    generator.output(1, true);
                    waitChecked(context, settle);

                    const double measuredRms = v7.readAcVoltage();
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
                    measuredGain[frequency] = calculatedGain;

                    MeasurementResult raw;
                    raw.parameterKey = "ubsi.yvp.raw." + std::to_string(channel + 1);
                    raw.title = "ЯВП " + std::to_string(channel + 1)
                        + ": K=" + std::to_string(gain)
                        + " мВ/пКл, " + std::to_string(frequency) + " Гц";
                    raw.reference = gain;
                    raw.measured = calculatedGain;
                    raw.unit = "мВ/пКл";
                    raw.verdict = RunVerdict::NotRun;
                    raw.message = "Диагностическая точка; итог формируют K/АЧХ";
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
                        {"calculated_gain_mv_per_pc",std::to_string(calculatedGain)}};
                    if (context.eventSink) context.eventSink({
                        std::chrono::system_clock::now(), step.id, "YVP_V7_POINT",
                        raw.title, RunVerdict::NotRun, raw.attributes});
                    result.measurements.push_back(std::move(raw));
                    generatorOff();
                }

                const auto ref = measuredGain.find(referenceFrequency);
                if (ref == measuredGain.end())
                    throw std::invalid_argument("ЯВП: отсутствует опорная частота 500 Гц");

                MeasurementResult gainResult;
                gainResult.parameterKey = "ubsi.yvp.gain." + std::to_string(channel + 1)
                    + "." + gainKey(gain);
                gainResult.title = "ЯВП " + std::to_string(channel + 1)
                    + ": коэффициент при " + std::to_string(referenceFrequency) + " Гц";
                gainResult.reference = gain;
                gainResult.measured = ref->second;
                gainResult.lowerLimit = gain * (1.0 - gainTolerance / 100.0);
                gainResult.upperLimit = gain * (1.0 + gainTolerance / 100.0);
                gainResult.unit = "мВ/пКл";
                gainResult.verdict = std::abs(relativeError(ref->second, gain)) <= gainTolerance
                    ? RunVerdict::Ok : RunVerdict::Fail;
                gainResult.message = gainResult.verdict == RunVerdict::Ok ? "Норма" : "Вне допуска ±7%";
                append(result, std::move(gainResult));

                for (const auto& [frequency,tolerance] :
                    std::vector<std::pair<double,double>>{{2,10},{6,5},{20,5},{1800,5},{2000,10}}) {
                    const auto point = measuredGain.find(frequency);
                    if (point == measuredGain.end())
                        throw std::invalid_argument("ЯВП: отсутствует обязательная точка АЧХ");
                    const double deviation = relativeError(point->second, ref->second);
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
                    afc.verdict = std::abs(deviation) <= tolerance ? RunVerdict::Ok : RunVerdict::Fail;
                    afc.message = afc.verdict == RunVerdict::Ok ? "Норма" : "Вне допуска АЧХ";
                    append(result, std::move(afc));
                }

                const auto high = measuredGain.find(4000.0);
                if (high == measuredGain.end())
                    throw std::invalid_argument("ЯВП: отсутствует точка затухания 4000 Гц");
                const double attenuation = attenuationDb(ref->second, high->second);
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
                attenuationResult.verdict = attenuation >= attenuationMinimum
                    ? RunVerdict::Ok : RunVerdict::Fail;
                attenuationResult.message = attenuationResult.verdict == RunVerdict::Ok
                    ? "Норма" : "Затухание ниже допустимого";
                append(result, std::move(attenuationResult));
            }
            safeReset();
        }
    } catch (...) {
        safeReset();
        throw;
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
