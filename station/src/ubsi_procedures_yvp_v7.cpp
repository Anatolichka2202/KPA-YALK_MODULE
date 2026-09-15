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

bool flag(const ScenarioNode& node, const std::string& key, bool fallback = false)
{
    const auto text = argument(node, key);
    if (text.empty()) return fallback;
    if (text == "true" || text == "1" || text == "yes") return true;
    if (text == "false" || text == "0" || text == "no") return false;
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

std::vector<unsigned> commissioningChannels(const ScenarioNode& node,
                                            unsigned channelCount)
{
    const auto found = node.arguments.find("commissioning_channels");
    if (found == node.arguments.end() || found->second.empty()) {
        std::vector<unsigned> result;
        for (unsigned channel = 1; channel <= channelCount; ++channel)
            result.push_back(channel - 1);
        return result;
    }

    std::vector<unsigned> result;
    std::set<unsigned> unique;
    std::stringstream stream(found->second);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (item.empty())
            throw std::invalid_argument("ЯВП V7/ИСД: пустой commissioning channel");
        std::size_t parsed = 0;
        const auto value = std::stoul(item, &parsed, 0);
        if (parsed != item.size() || value < 1 || value > channelCount
            || !unique.insert(static_cast<unsigned>(value)).second) {
            throw std::invalid_argument(
                "ЯВП V7/ИСД: commissioning_channels должен содержать уникальные номера 1..8");
        }
        result.push_back(static_cast<unsigned>(value - 1));
    }
    if (result.empty())
        throw std::invalid_argument("ЯВП V7/ИСД: commissioning_channels не задан");
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

    // Новый production backend намеренно не использует адаптер КТМА/ROKT для
    // измерения. До подтверждения Э3-карты ИСД никакие активные коммутации и
    // OUTPUT генератора не выполняются.
    if (!flag(node, "mapping_confirmed")) {
        return {RunVerdict::Incomplete,
            "ЯВП V7/ИСД: backend выбран, но карта ИСД (входы, КУ и CH89..CH96 -> В7) ещё не подтверждена; воздействие не выполнялось",
            {}};
    }
    if (!flag(node, "active_outputs_confirmed")) {
        return {RunVerdict::Incomplete,
            "ЯВП V7/ИСД: активное воздействие Rigol не разрешено сценарием", {}};
    }

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
    const bool hasPerChannelGainMap =
        node.arguments.count("channel_1_gain_contacts") != 0;
    if (hasPerChannelGainMap) {
        std::vector<std::vector<unsigned>> channelGainContacts(channelCount);
        for (unsigned channel = 0; channel < channelCount; ++channel) {
            const auto key = "channel_" + std::to_string(channel + 1)
                + "_gain_contacts";
            channelGainContacts[channel] = contacts(node, key);
            if (channelGainContacts[channel].size() != 4)
                throw std::invalid_argument(
                    "ЯВП V7/ИСД: " + key + " должен содержать KU1..KU4");
        }
        for (const double gain : gains) {
            const auto bits = contacts(node, gainBitsKey(gain), true);
            std::set<unsigned> uniqueBits;
            for (const unsigned bit : bits) {
                if (bit < 1 || bit > 4 || !uniqueBits.insert(bit).second)
                    throw std::invalid_argument(
                        "ЯВП V7/ИСД: карта " + gainBitsKey(gain)
                        + " должна содержать уникальные KU-биты 1..4");
            }
            for (unsigned channel = 0; channel < channelCount; ++channel) {
                auto& row = gainMap[channel][gain];
                for (const unsigned bit : bits)
                    row.push_back(channelGainContacts[channel][bit - 1]);
            }
        }
    } else {
        // Compatibility for synthetic and retained commissioning scenarios.
        for (const double gain : gains) {
            const auto row = contacts(node, gainKey(gain), true);
            for (auto& channelMap : gainMap) channelMap[gain] = row;
        }
    }

    const double capacitancePf = number(node, "coupling_capacitance_pf", 1000.0);
    if (!(capacitancePf > 0.0))
        throw std::invalid_argument("ЯВП V7/ИСД: coupling_capacitance_pf должен быть > 0");
    const unsigned settleMs = natural(node, "settle_ms", 200);
    const auto selectedChannels = commissioningChannels(node, channelCount);

    auto generatorOff = [&] {
        context.equipment.invoke("signal.generator", "output", {
            {"channel", "1"}, {"enabled", "false"}});
    };
    auto safeReset = [&] {
        try { generatorOff(); } catch (...) {}
        // type=4 is the confirmed firmware command for a complete ISD reset.
        // Sending an OFF request for every possible YVP contact first creates
        // dozens of redundant HTTP transactions and overruns the live ISD.
        try { context.equipment.invoke("stand.switch_matrix", "full_reset", {}); } catch (...) {}
    };

    ProcedureResult result{RunVerdict::Incomplete,
        "ЯВП V7/ИСД: измерительная матрица выполнена без приёмочного verdict; критерии будут подключены после commissioning",
        {}};

    try {
        safeReset();
        for (const unsigned channel : selectedChannels) {
            setContacts(context, inputType, inputMap[channel], true);
            setMeasurementContacts(context, measurementAnalogType, measurementType,
                                   measurementMap[channel], true);

            std::vector<unsigned> activeGainContacts;
            for (const double gain : gains) {
                generatorOff();
                setContacts(context, gainType, activeGainContacts, false);
                activeGainContacts = gainMap[channel].at(gain);
                setContacts(context, gainType, activeGainContacts, true);
                const double inputVpp = yvpStimulusVppForGain(gain);

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
                        measuredFrequencyText = std::to_string(responseNumber(
                            context.equipment.invoke(
                                "measure.reference_frequency", "read_frequency", {}),
                            "hertz"));
                        frequencyVerification = "v7";
                    } else {
                        frequencyVerification = "unavailable_by_v7";
                    }
                    const double outputVpp = measuredRms * 2.0 * std::sqrt(2.0);
                    const double chargePc = yvpChargePc(capacitancePf, inputVpp);
                    const double calculatedGain = yvpGainMvPerPc(outputVpp, chargePc);

                    MeasurementResult point;
                    point.parameterKey = "ubsi.yvp.v7_isd." + std::to_string(channel + 1);
                    point.title = "ЯВП " + std::to_string(channel + 1)
                        + ": K=" + std::to_string(gain)
                        + " мВ/пКл, " + std::to_string(frequency) + " Гц";
                    point.reference = gain;
                    point.measured = calculatedGain;
                    point.lowerLimit = 0.0;
                    point.upperLimit = 0.0;
                    point.unit = "мВ/пКл";
                    point.verdict = RunVerdict::NotRun;
                    point.message = "Commissioning: измерение В7 сохранено без приёмочного критерия";
                    point.attributes = {
                        {"backend", "v7_isd"},
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
                        {"acceptance", "not_applied"}};
                    context.eventSink({std::chrono::system_clock::now(), node.id,
                        "YVP_V7_POINT", point.title, RunVerdict::NotRun, point.attributes});
                    result.measurements.push_back(std::move(point));

                    generatorOff();
                }
            }

            safeReset();
        }
    } catch (...) {
        safeReset();
        throw;
    }

    return result;
}

} // namespace

void registerYvpV7Procedures(ScenarioEngine& engine)
{
    engine.registerProcedure("yvp.v7_isd", yvpV7Isd);
    // Production alias: old experimental backends keep their own ids and are
    // no longer selected by ubsi.yvp.
    engine.registerProcedure("ubsi.yvp", yvpV7Isd);
}

} // namespace orbita::stand
