#include "orbita_stand/ubsi_procedures.h"
#include "orbita_stand/ubsi_yvp_math.h"

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
        if (!item.empty()) result.push_back(std::stod(item));
    }
    return result;
}

std::vector<unsigned> contacts(const ScenarioNode& node, const std::string& key,
                               bool allowNone = false)
{
    const auto found = node.arguments.find(key);
    if (found == node.arguments.end())
        throw std::invalid_argument("ЯВП V7/ИСД: отсутствует карта " + key);
    const std::string text = found->second;
    if (allowNone && (text.empty() || text == "none" || text == "-")) return {};
    std::vector<unsigned> result;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (item.empty()) continue;
        std::size_t parsed = 0;
        const auto value = std::stoul(item, &parsed, 0);
        if (parsed != item.size() || value == 0)
            throw std::invalid_argument("ЯВП V7/ИСД: некорректный контакт в " + key);
        result.push_back(static_cast<unsigned>(value));
    }
    if (!allowNone && result.empty())
        throw std::invalid_argument("ЯВП V7/ИСД: пустая карта " + key);
    return result;
}

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

double responseNumber(const std::string& response, const std::string& key)
{
    const auto values = responseValues(response);
    const auto found = values.find(key);
    if (found == values.end()) throw std::runtime_error("Оборудование не вернуло поле " + key);
    return std::stod(found->second);
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

std::string gainKey(double gain)
{
    if (std::abs(gain - 0.25) < 1e-9) return "gain_0_25_contacts";
    if (std::abs(gain - 0.5) < 1e-9) return "gain_0_5_contacts";
    if (std::abs(gain - 1.0) < 1e-9) return "gain_1_contacts";
    if (std::abs(gain - 2.0) < 1e-9) return "gain_2_contacts";
    if (std::abs(gain - 4.0) < 1e-9) return "gain_4_contacts";
    if (std::abs(gain - 8.0) < 1e-9) return "gain_8_contacts";
    if (std::abs(gain - 32.0) < 1e-9) return "gain_32_contacts";
    throw std::invalid_argument("ЯВП V7/ИСД: коэффициент не входит в методику 0.25/0.5/1/2/4/8/32");
}

void setContacts(ProcedureContext& context, unsigned type,
                 const std::vector<unsigned>& values, bool enabled)
{
    for (const unsigned channel : values) {
        context.equipment.invoke("stand.switch_matrix", "switch", {
            {"type", std::to_string(type)},
            {"channel", std::to_string(channel)},
            {"enabled", enabled ? "true" : "false"}});
    }
}

std::string gainBitsKey(double gain)
{
    auto key = gainKey(gain);
    const auto suffix = key.rfind("_contacts");
    key.replace(suffix, std::string("_contacts").size(), "_bits");
    return key;
}

void setMeasurementContacts(ProcedureContext& context, unsigned analogType,
                            unsigned switchType, const std::vector<unsigned>& values,
                            bool enabled)
{
    // The KPA overload scenario routes an externally driven line by disabling
    // its DM output first (type=1 work=0), then connecting the same analog
    // channel to the common V7 bus with type=3.  Enabling the DM output here
    // would drive and load the YVP output instead of measuring it.
    for (const unsigned channel : values) {
        if (enabled && analogType) {
            context.equipment.invoke("stand.switch_matrix", "analog", {
                {"channel", std::to_string(channel)}, {"value", "0"},
                {"enabled", "false"}});
        }
        context.equipment.invoke("stand.switch_matrix", "switch", {
            {"type", std::to_string(switchType)},
            {"channel", std::to_string(channel)},
            {"enabled", enabled ? "true" : "false"}});
        if (!enabled && analogType) {
            context.equipment.invoke("stand.switch_matrix", "analog", {
                {"channel", std::to_string(channel)}, {"value", "0"},
                {"enabled", "false"}});
        }
    }
}

ProcedureResult yvpV7Isd(const ScenarioNode& node, ProcedureContext& context)
{
    const unsigned channelCount = natural(node, "channel_count", 8);
    if (channelCount != 8)
        throw std::invalid_argument("ЯВП V7/ИСД: конструктив УБСИ содержит 8 каналов ЯВП-8");

    const auto gains = numbers(node, "gains_mv_per_pcl");
    const auto frequencies = numbers(node, "frequencies_hz");
    if (gains.empty() || frequencies.empty())
        throw std::invalid_argument("ЯВП V7/ИСД: не заданы коэффициенты или частоты методики");
    for (const double gain : gains) (void)gainKey(gain);

    const unsigned inputType = natural(node, "input_switch_type");
    const unsigned gainType = natural(node, "gain_switch_type");
    const unsigned measurementType = natural(node, "measurement_switch_type");
    if (!inputType || !gainType || !measurementType)
        throw std::invalid_argument("ЯВП V7/ИСД: нужны подтверждённые input/gain/measurement switch type");
    const unsigned measurementAnalogType = natural(node, "measurement_analog_type", 0);

    std::vector<std::vector<unsigned>> inputMap(channelCount);
    std::vector<std::vector<unsigned>> measurementMap(channelCount);
    for (unsigned channel = 0; channel < channelCount; ++channel) {
        inputMap[channel] = contacts(node, "input_" + std::to_string(channel + 1) + "_contacts");
        measurementMap[channel] = contacts(
            node, "measurement_" + std::to_string(channel + 1) + "_contacts");
    }

    // Four KU controls are physically separate for every YVP channel.  The
    // reference KPA scenarios define the coefficient as a KU-bit pattern;
    // the E3/firmware map defines which HTTP type=2 contact implements each
    // KU bit on each channel.  Keep both parts explicit instead of deriving
    // contacts with base+offset arithmetic.
    std::vector<std::map<double, std::vector<unsigned>>> gainMap(channelCount);
    std::vector<std::vector<unsigned>> channelGainContacts(channelCount);
    for (unsigned channel = 0; channel < channelCount; ++channel) {
        const auto key = "channel_" + std::to_string(channel + 1) + "_gain_contacts";
        channelGainContacts[channel] = contacts(node, key);
        if (channelGainContacts[channel].size() != 4)
            throw std::invalid_argument("ЯВП: " + key + " должен содержать KU1..KU4");
    }
    for (const double gain : gains) {
        const auto bits = contacts(node, gainBitsKey(gain), true);
        std::set<unsigned> uniqueBits;
        for (const unsigned bit : bits) {
            if (bit < 1 || bit > 4 || !uniqueBits.insert(bit).second)
                throw std::invalid_argument("ЯВП: карта KU должна содержать уникальные биты 1..4");
        }
        for (unsigned channel = 0; channel < channelCount; ++channel) {
            auto& row = gainMap[channel][gain];
            for (const unsigned bit : bits) row.push_back(channelGainContacts[channel][bit - 1]);
        }
    }

    if (!node.arguments.count("coupling_capacitance_pf"))
        throw std::invalid_argument("ЯВП: coupling_capacitance_pf обязан быть задан сценарием");
    const double capacitancePf = number(node, "coupling_capacitance_pf");
    if (!(capacitancePf > 0.0))
        throw std::invalid_argument("ЯВП V7/ИСД: coupling_capacitance_pf должен быть > 0");
    const unsigned settleMs = natural(node, "settle_ms", 200);
    const double referenceFrequency = number(node, "reference_frequency_hz");
    const double gainTolerance = number(node, "gain_tolerance_percent");
    const double attenuationMinimum = number(node, "attenuation_min_db");
    if (!(referenceFrequency > 0.0) || !(gainTolerance > 0.0)
        || !(attenuationMinimum > 0.0))
        throw std::invalid_argument("ЯВП: не заданы production-критерии");

    auto generatorOff = [&] {
        context.equipment.invoke("signal.generator", "output", {
            {"channel", "1"}, {"enabled", "false"}});
    };
    std::vector<unsigned> activeInputContacts;
    std::vector<unsigned> activeMeasurementContacts;
    std::vector<unsigned> activeGainContacts;
    auto disableContacts = [&](unsigned type, std::vector<unsigned>& active) {
        for (auto contact = active.rbegin(); contact != active.rend(); ++contact) {
            try { setContacts(context, type, {*contact}, false); } catch (...) {}
        }
        active.clear();
    };
    auto disableMeasurementContacts = [&] {
        for (auto contact = activeMeasurementContacts.rbegin();
             contact != activeMeasurementContacts.rend(); ++contact) {
            try {
                setMeasurementContacts(context, measurementAnalogType, measurementType,
                                       {*contact}, false);
            } catch (...) {}
        }
        activeMeasurementContacts.clear();
    };
    auto safeReset = [&] {
        // The live ISD can leave its single HTTP worker blocked in type=4 when
        // one of the internal modules does not answer.  YVP owns every route it
        // enables, so clean up only those routes and always remove Rigol first.
        try { generatorOff(); } catch (...) {}
        disableContacts(gainType, activeGainContacts);
        disableMeasurementContacts();
        disableContacts(inputType, activeInputContacts);
    };

    ProcedureResult result{RunVerdict::Ok, "ЯВП-8 соответствует производственной методике", {}};
    auto addAcceptance = [&](MeasurementResult value) {
        result.verdict = combineVerdicts(result.verdict, value.verdict);
        result.measurements.push_back(std::move(value));
    };

    try {
        // Read-only HTTP readiness must succeed before any active route or Rigol output.
        context.equipment.invoke("stand.switch_matrix", "probe", {});
        safeReset();
        for (unsigned channel = 0; channel < channelCount; ++channel) {
            for (const unsigned contact : inputMap[channel]) {
                setContacts(context, inputType, {contact}, true);
                activeInputContacts.push_back(contact);
            }
            for (const unsigned contact : measurementMap[channel]) {
                setMeasurementContacts(context, measurementAnalogType, measurementType,
                                       {contact}, true);
                activeMeasurementContacts.push_back(contact);
            }

            for (const double gain : gains) {
                generatorOff();
                disableContacts(gainType, activeGainContacts);
                for (const unsigned contact : gainMap[channel].at(gain)) {
                    setContacts(context, gainType, {contact}, true);
                    activeGainContacts.push_back(contact);
                }
                const double inputVpp = yvpStimulusVppForGain(gain);

                std::map<double, double> measuredGain;
                for (const double frequency : frequencies) {
                    context.equipment.invoke("signal.generator", "set_sine", {
                        {"channel", "1"},
                        {"frequency_hz", std::to_string(frequency)},
                        {"amplitude_vpp", std::to_string(inputVpp)},
                        {"offset_v", "0"}});
                    context.equipment.invoke("signal.generator", "output", {
                        {"channel", "1"}, {"enabled", "true"}});
                    waitScaled(context, settleMs);

                    const double measuredRms = responseNumber(context.equipment.invoke(
                        "measure.reference_ac_voltage", "read_ac_voltage", {}), "volts");
                    std::string measuredFrequencyText;
                    std::string frequencyVerification;
                    if (frequency >= 10.0) {
                        try {
                            measuredFrequencyText = std::to_string(responseNumber(
                                context.equipment.invoke(
                                    "measure.reference_frequency", "read_frequency", {}),
                                "hertz"));
                            frequencyVerification = "v7_diagnostic";
                        } catch (...) {
                            frequencyVerification = "unavailable_by_v7";
                        }
                    } else {
                        frequencyVerification = "unavailable_by_v7";
                    }
                    const double outputVpp = measuredRms * 2.0 * std::sqrt(2.0);
                    const double chargePc = yvpChargePc(capacitancePf, inputVpp);
                    const double calculatedGain = yvpGainMvPerPc(outputVpp, chargePc);
                    measuredGain[frequency] = calculatedGain;

                    MeasurementResult point;
                    point.parameterKey = "ubsi.yvp.raw." + std::to_string(channel + 1);
                    point.title = "ЯВП " + std::to_string(channel + 1)
                        + ": K=" + std::to_string(gain)
                        + " мВ/пКл, " + std::to_string(frequency) + " Гц";
                    point.reference = gain;
                    point.measured = calculatedGain;
                    point.lowerLimit = 0.0;
                    point.upperLimit = 0.0;
                    point.unit = "мВ/пКл";
                    point.verdict = RunVerdict::NotRun;
                    point.message = "Диагностическое измерение В7; verdict формируется критериями K/АЧХ";
                    point.attributes = {
                        {"backend", "production_isd_v7"},
                        {"yvp_channel", std::to_string(channel + 1)},
                        {"gain_mv_per_pc", std::to_string(gain)},
                        {"set_frequency_hz", std::to_string(frequency)},
                        {"measured_frequency_hz", measuredFrequencyText},
                        {"frequency_verification", frequencyVerification},
                        {"rigol_input_vpp", std::to_string(inputVpp)},
                        {"v7_output_vrms", std::to_string(measuredRms)},
                        {"v7_output_vpp", std::to_string(outputVpp)},
                        {"capacitance_pf", std::to_string(capacitancePf)},
                        {"charge_pc_from_commanded_vpp", std::to_string(chargePc)},
                        {"calculated_gain_mv_per_pc", std::to_string(calculatedGain)},
                        {"acceptance", "evaluated_after_gain_sweep"}};
                    context.eventSink({std::chrono::system_clock::now(), node.id,
                        "YVP_V7_POINT", point.title, RunVerdict::NotRun, point.attributes});
                    result.measurements.push_back(std::move(point));

                    generatorOff();
                }

                const auto reference = measuredGain.find(referenceFrequency);
                if (reference == measuredGain.end())
                    throw std::invalid_argument("ЯВП: reference_frequency_hz отсутствует в frequencies_hz");

                MeasurementResult gainResult;
                gainResult.parameterKey = "ubsi.yvp.gain." + std::to_string(channel + 1)
                    + "." + gainKey(gain);
                gainResult.title = "ЯВП " + std::to_string(channel + 1)
                    + ": коэффициент при " + std::to_string(referenceFrequency) + " Гц";
                gainResult.reference = gain;
                gainResult.measured = reference->second;
                gainResult.lowerLimit = gain * (1.0 - gainTolerance / 100.0);
                gainResult.upperLimit = gain * (1.0 + gainTolerance / 100.0);
                gainResult.unit = "мВ/пКл";
                gainResult.verdict = std::abs(yvpRelativeErrorPercent(reference->second, gain))
                        <= gainTolerance ? RunVerdict::Ok : RunVerdict::Fail;
                gainResult.message = gainResult.verdict == RunVerdict::Ok ? "Норма" : "Вне допуска ±7%";
                addAcceptance(std::move(gainResult));

                for (const auto& [frequency, tolerance] :
                     std::vector<std::pair<double, double>>{{2,10},{6,5},{20,5},{1800,5},{2000,10}}) {
                    const auto point = measuredGain.find(frequency);
                    if (point == measuredGain.end())
                        throw std::invalid_argument("ЯВП: отсутствует обязательная точка АЧХ");
                    const double deviation = yvpAfcPercent(point->second, reference->second);
                    MeasurementResult afc;
                    afc.parameterKey = "ubsi.yvp.afc." + std::to_string(channel + 1)
                        + "." + std::to_string(static_cast<unsigned>(frequency));
                    afc.title = "ЯВП " + std::to_string(channel + 1) + ": АЧХ "
                        + std::to_string(frequency) + " Гц относительно 500 Гц";
                    afc.reference = 0.0; afc.measured = deviation;
                    afc.lowerLimit = -tolerance; afc.upperLimit = tolerance; afc.unit = "%";
                    afc.verdict = std::abs(deviation) <= tolerance ? RunVerdict::Ok : RunVerdict::Fail;
                    afc.message = afc.verdict == RunVerdict::Ok ? "Норма" : "Вне допуска АЧХ";
                    addAcceptance(std::move(afc));
                }

                const auto high = measuredGain.find(4000.0);
                if (high == measuredGain.end())
                    throw std::invalid_argument("ЯВП: отсутствует обязательная точка 4000 Гц");
                const double attenuation = yvpAttenuationDb(reference->second, high->second);
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
                addAcceptance(std::move(attenuationResult));
            }

            safeReset();
        }
    } catch (...) {
        safeReset();
        throw;
    }

    if (result.verdict == RunVerdict::Fail)
        result.message = "ЯВП-8 не соответствует производственной методике";
    return result;
}

} // namespace

void registerProductionYvpProcedure(ScenarioEngine& engine)
{
    engine.registerProcedure("ubsi.yvp", yvpV7Isd);
}

} // namespace orbita::stand
