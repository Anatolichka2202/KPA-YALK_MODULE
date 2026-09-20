#include "procedures/yalk_verified_procedures.h"

#include "hardware/stand_hardware.h"

#include <chrono>
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
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

std::vector<unsigned> addressList(const std::string& text)
{
    std::vector<unsigned> result;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (item.empty()) continue;
        const auto dash = item.find('-');
        if (dash == std::string::npos) {
            result.push_back(static_cast<unsigned>(std::stoul(item)));
            continue;
        }
        const unsigned first = static_cast<unsigned>(std::stoul(item.substr(0, dash)));
        const unsigned last = static_cast<unsigned>(std::stoul(item.substr(dash + 1)));
        if (!first || last < first)
            throw std::invalid_argument("Некорректный диапазон адресов: " + item);
        for (unsigned value = first; value <= last; ++value) result.push_back(value);
    }
    return result;
}

std::vector<unsigned> analogAddresses(const ScenarioStep& step)
{
    auto values = addressList(argument(step, "addresses",
        "1-28,32-43,45-70,74-87"));
    if (values.size() != 80)
        throw std::invalid_argument("Карта аналоговых входов ЯЛК должна содержать ровно 80 адресов");
    return values;
}

double stateNumber(const ProcedureContext& context, const std::string& key)
{
    const auto found = context.state.find(key);
    if (found == context.state.end()) throw std::runtime_error("Нет состояния ЯЛК: " + key);
    return std::stod(found->second);
}

double yalkVolts(double code, const ProcedureContext& context)
{
    const double zero = stateNumber(context, "yalk.zero_code");
    const double full = stateNumber(context, "yalk.full_code");
    const double fullVoltage = stateNumber(context, "yalk.full_voltage");
    if (!(full > zero)) throw std::runtime_error("Неверная калибровка ЯЛК");
    return (code - zero) * fullVoltage / (full - zero);
}

void publish(ProcedureContext& context, const ScenarioStep& step,
             const std::string& stage, const std::string& message,
             RunVerdict verdict, std::map<std::string, std::string> data)
{
    if (context.eventSink) context.eventSink({
        std::chrono::system_clock::now(), step.id, stage, message, verdict, std::move(data)});
}

ProcedureResult analogOpenCircuit(
    const ScenarioStep& step,
    ProcedureContext& context,
    const std::shared_ptr<hardware::StandHardware>& stand)
{
    const auto addresses = analogAddresses(step);
    const auto frame = stand->yalk().readYalkSnapshot(
        natural(step, "sample_count", 4),
        std::chrono::milliseconds(natural(step, "timeout_ms", 3000)),
        [&context] { context.checkpoint(); });

    const double fullScale = number(step, "full_scale_v", 6.2);
    ProcedureResult result{RunVerdict::Ok,
        "Проверен обрыв 80 функциональных аналоговых входов ЯЛК", {}};

    for (std::size_t index = 0; index < addresses.size(); ++index) {
        context.checkpoint();
        const unsigned address = addresses[index];
        const auto& reading = frame.at(address - 1);
        const double volts = yalkVolts(reading.codeMean, context);

        MeasurementResult value;
        value.parameterKey = "ubsi.yalk.open_circuit." + std::to_string(address);
        value.title = "ЯЛК адрес " + std::to_string(address) + ": обрыв";
        value.reference = 0.0;
        value.measured = volts;
        value.lowerLimit = -fullScale;
        value.upperLimit = -1e-12;
        value.unit = "В";
        value.verdict = volts < 0.0 ? RunVerdict::Ok : RunVerdict::Fail;
        value.message = value.verdict == RunVerdict::Ok
            ? "Норма"
            : "При обрыве функционального аналогового входа значение должно быть ниже 0 В";
        value.attributes = {
            {"section", "YALK"},
            {"channel", std::to_string(index + 1)},
            {"channel_index", std::to_string(index + 1)},
            {"channel_count", std::to_string(addresses.size())},
            {"ulk_address", std::to_string(address)},
            {"raw", std::to_string(reading.rawMean)},
            {"analog_code", std::to_string(reading.codeMean)},
            {"yalk_v", std::to_string(volts)},
            {"analog_ok", value.verdict == RunVerdict::Ok ? "1" : "0"},
            {"signal_check", "not_applicable"},
        };

        result.verdict = combineVerdicts(result.verdict, value.verdict);
        publish(context, step, "YALK_INITIAL", value.title, value.verdict, value.attributes);
        result.measurements.push_back(std::move(value));
    }

    return result;
}

} // namespace

void registerVerifiedYalkProcedures(
    ScenarioEngine& engine,
    std::shared_ptr<hardware::StandHardware> hardware)
{
    if (!hardware) throw std::invalid_argument("StandHardware is required");
    engine.registerProcedure("yalk.open_circuit", [hardware](const auto& step, auto& context) {
        return analogOpenCircuit(step, context, hardware);
    });
}

} // namespace tu::procedures
