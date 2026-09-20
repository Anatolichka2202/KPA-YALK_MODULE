#include "procedures/yalk_procedures.h"

#include "hardware/stand_hardware.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
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

std::vector<unsigned> yalkAddresses(const ScenarioStep& step)
{
    auto values = addressList(argument(step, "addresses",
        "1-28,32-43,45-70,74-87"));
    if (values.size() != 80)
        throw std::invalid_argument("Подтверждённая карта ЯЛК должна содержать 80 адресов");
    return values;
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

void publish(ProcedureContext& context, const ScenarioStep& step,
             const std::string& stage, const std::string& message,
             RunVerdict verdict = RunVerdict::NotRun,
             std::map<std::string, std::string> data = {})
{
    if (context.eventSink) context.eventSink({
        std::chrono::system_clock::now(), step.id, stage, message, verdict, std::move(data)});
}

void publishMeasurement(ProcedureContext& context, const ScenarioStep& step,
                        const MeasurementResult& value,
                        const std::string& stage = "MEASUREMENT")
{
    publish(context, step, stage, value.title, value.verdict, value.attributes);
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

ProcedureResult isdBaseline(const ScenarioStep&, ProcedureContext& context,
                            const std::shared_ptr<hardware::StandHardware>& stand)
{
    context.checkpoint();
    stand->isd().serviceFullReset();
    return {RunVerdict::Ok,
        "Стартовый all-off baseline ИСД выполнен подтверждённой service-командой type=4", {}};
}

ProcedureResult start(const ScenarioStep& step, ProcedureContext& context,
                      const std::shared_ptr<hardware::StandHardware>& stand)
{
    const bool ready = stand->yalk().startYalk(
        std::chrono::milliseconds(natural(step, "configure_settle_ms", 500)),
        std::chrono::milliseconds(natural(step, "timeout_ms", 3000)),
        [&context] { context.checkpoint(); });
    if (!ready) return {RunVerdict::Fail, "Не получен reference204 кадр ЯЛК", {}};
    return {RunVerdict::Ok, "ROKT ЯЛК запущен, получен reference204 кадр", {}};
}

ProcedureResult calibration(const ScenarioStep& step, ProcedureContext& context,
                            const std::shared_ptr<hardware::StandHardware>& stand)
{
    const auto addresses = yalkAddresses(step);
    const unsigned first = addresses.front();
    const unsigned samples = natural(step, "sample_count", 16);
    const unsigned settle = natural(step, "settle_ms", 150);
    const double fullVoltage = number(step, "full_voltage", 6.2);

    try {
        stand->isd().setYalkVoltage(first, fullVoltage);
        waitChecked(context, settle);
        const double reference = stand->v7().readDcVoltage();
        const auto frame = stand->yalk().readYalkSnapshot(
            samples, std::chrono::milliseconds(3000), [&context] { context.checkpoint(); });
        stand->isd().disableYalkOutput(first);

        const double zero = frame.at(96).codeMean; // адрес 97
        const double full = frame.at(98).codeMean; // адрес 99
        if (!(full > zero) || reference < 5.5 || reference > 6.8)
            throw std::runtime_error("Недостоверная калибровка ЯЛК 97/99 или напряжение В7");

        context.state["yalk.zero_code"] = std::to_string(zero);
        context.state["yalk.full_code"] = std::to_string(full);
        context.state["yalk.full_voltage"] = std::to_string(fullVoltage);
        context.state["yalk.calibration_reference_v"] = std::to_string(reference);

        ProcedureResult result{RunVerdict::Ok, "Снята калибровка ЯЛК по адресам 97/99", {}};
        auto value = measurement("ubsi.yalk.calibration", "Опорное воздействие калибровки ЯЛК",
                                 6.2, reference, 5.5, 6.8, "В");
        value.attributes = {{"zero_address","97"},{"full_address","99"},
                            {"zero_code",std::to_string(zero)},
                            {"full_code",std::to_string(full)},
                            {"scale_voltage_v",std::to_string(fullVoltage)},
                            {"v7_v",std::to_string(reference)}};
        publishMeasurement(context, step, value);
        append(result, std::move(value));
        return result;
    } catch (...) {
        try { stand->isd().disableYalkOutput(first); } catch (...) {}
        throw;
    }
}

ProcedureResult initial(const ScenarioStep& step, ProcedureContext& context,
                        const std::shared_ptr<hardware::StandHardware>& stand)
{
    const auto addresses = yalkAddresses(step);
    const auto frame = stand->yalk().readYalkSnapshot(
        natural(step, "sample_count", 4), std::chrono::milliseconds(3000),
        [&context] { context.checkpoint(); });
    ProcedureResult result{RunVerdict::Ok, "Проверено отключённое состояние 80 входов ЯЛК", {}};

    for (std::size_t channelIndex = 0; channelIndex < addresses.size(); ++channelIndex) {
        context.checkpoint();
        const unsigned address = addresses[channelIndex];
        const auto& reading = frame.at(address - 1);
        const double volts = yalkVolts(reading.codeMean, context);

        auto analog = measurement("ubsi.yalk.initial." + std::to_string(address),
            "ЯЛК адрес " + std::to_string(address) + ": обрыв", 0.0, volts,
            -number(step,"full_scale_v",6.2), -1e-12, "В");
        if (!(volts < 0.0)) {
            analog.verdict = RunVerdict::Fail;
            analog.message = "При обрыве значение должно быть ниже 0 В";
        }
        analog.attributes = {{"ulk_address",std::to_string(address)},
                             {"raw",std::to_string(reading.rawMean)},
                             {"analog_code",std::to_string(reading.codeMean)},
                             {"yalk_v",std::to_string(volts)},
                             {"signal",reading.contact?"1":"0"}};

        auto signal = measurement("ubsi.yalk.initial.signal." + std::to_string(address),
            "ЯЛК адрес " + std::to_string(address) + ": исходный сигнал",
            1.0, reading.contact ? 1.0 : 0.0, 1.0, 1.0, "лог.");

        const RunVerdict channelVerdict = combineVerdicts(analog.verdict, signal.verdict);
        auto eventData = analog.attributes;
        eventData["channel_index"] = std::to_string(channelIndex + 1);
        eventData["channel_count"] = std::to_string(addresses.size());
        eventData["expected_signal"] = "1";
        eventData["analog_ok"] = analog.verdict == RunVerdict::Ok ? "1" : "0";
        eventData["signal_ok"] = signal.verdict == RunVerdict::Ok ? "1" : "0";
        publish(context, step, "YALK_INITIAL", analog.title, channelVerdict, std::move(eventData));

        append(result, std::move(analog));
        append(result, std::move(signal));
    }
    return result;
}

ProcedureResult channelSweep(const ScenarioStep& step, ProcedureContext& context,
                             const std::shared_ptr<hardware::StandHardware>& stand,
                             bool thresholds)
{
    const auto addresses = yalkAddresses(step);
    const auto points = numbers(step, thresholds ? "contact_points_v" : "point_volts");
    const auto expected = thresholds ? numbers(step, "signal_expectations") : std::vector<double>{};
    if (points.empty() || (thresholds && expected.size() != points.size()))
        throw std::invalid_argument("Не заполнены точки ЯЛК");

    const unsigned samples = natural(step, "sample_count", 16);
    const unsigned settle = natural(step, "settle_ms", 150);
    const unsigned offSettle = natural(step, "channel_off_settle_ms", 1000);
    const double fullScale = number(step, "full_scale_v", 6.2);
    const double tolerance = fullScale
        * number(step, "tolerance_percent_fs", 0.5) / 100.0;
    ProcedureResult result{RunVerdict::Ok,
        thresholds ? "Проверены контактные пороги 80 адресов ЯЛК"
                   : "Проверены аналоговые каналы ЯЛК", {}};

    for (std::size_t pointIndex = 0; pointIndex < points.size(); ++pointIndex) {
        publish(context, step, "YALK_POINT",
            std::string(thresholds ? "Контактная точка " : "Аналоговая точка ")
                + std::to_string(points[pointIndex]) + " В",
            RunVerdict::NotRun,
            {{"point_index",std::to_string(pointIndex + 1)},
             {"point_count",std::to_string(points.size())},
             {"command_v",std::to_string(points[pointIndex])}});

        for (std::size_t channelIndex = 0; channelIndex < addresses.size(); ++channelIndex) {
            context.checkpoint();
            const unsigned address = addresses[channelIndex];
            try {
                stand->isd().setYalkVoltage(address, points[pointIndex]);
                waitChecked(context, settle);
                const double reference = stand->v7().readDcVoltage();
                const auto frame = stand->yalk().readYalkSnapshot(
                    samples, std::chrono::milliseconds(3000),
                    [&context] { context.checkpoint(); });
                const auto& reading = frame.at(address - 1);
                const double volts = yalkVolts(reading.codeMean, context);
                const double absolute = std::abs(volts - reference);
                const double reduced = absolute / fullScale * 100.0;

                if (!thresholds) {
                    auto analog = measurement("ubsi.yalk.channel." + std::to_string(address)
                            + "." + std::to_string(pointIndex),
                        "ЯЛК адрес " + std::to_string(address) + " · "
                            + std::to_string(points[pointIndex]) + " В",
                        reference, volts, reference - tolerance, reference + tolerance, "В");
                    analog.attributes = {{"section","YALK"},
                        {"channel",std::to_string(channelIndex + 1)},
                        {"channel_index",std::to_string(channelIndex + 1)},
                        {"channel_count",std::to_string(addresses.size())},
                        {"ulk_address",std::to_string(address)},
                        {"command_v",std::to_string(points[pointIndex])},
                        {"point_index",std::to_string(pointIndex + 1)},
                        {"point_count",std::to_string(points.size())},
                        {"scan_order","point_major"},
                        {"v7_v",std::to_string(reference)},
                        {"yalk_v",std::to_string(volts)},
                        {"raw",std::to_string(reading.rawMean)},
                        {"analog_code",std::to_string(reading.codeMean)},
                        {"absolute_error_v",std::to_string(absolute)},
                        {"lower_limit_v",std::to_string(analog.lowerLimit)},
                        {"upper_limit_v",std::to_string(analog.upperLimit)},
                        {"reduced_error_percent",std::to_string(reduced)}};
                    publishMeasurement(context, step, analog);
                    append(result, std::move(analog));
                } else {
                    const bool expectedSignal = expected[pointIndex] >= 0.5;
                    auto signal = measurement("ubsi.yalk.signal." + std::to_string(address)
                            + "." + std::to_string(pointIndex),
                        "ЯЛК адрес " + std::to_string(address) + ": контакт при "
                            + std::to_string(points[pointIndex]) + " В",
                        expectedSignal ? 1.0 : 0.0,
                        reading.contact ? 1.0 : 0.0,
                        expectedSignal ? 1.0 : 0.0,
                        expectedSignal ? 1.0 : 0.0, "лог.");
                    signal.attributes = {{"section","YALK"},
                        {"channel",std::to_string(channelIndex + 1)},
                        {"channel_index",std::to_string(channelIndex + 1)},
                        {"channel_count",std::to_string(addresses.size())},
                        {"ulk_address",std::to_string(address)},
                        {"command_v",std::to_string(points[pointIndex])},
                        {"point_index",std::to_string(pointIndex + 1)},
                        {"point_count",std::to_string(points.size())},
                        {"scan_order","point_major"},
                        {"v7_v",std::to_string(reference)},
                        {"yalk_v",std::to_string(volts)},
                        {"raw",std::to_string(reading.rawMean)},
                        {"analog_code",std::to_string(reading.codeMean)},
                        {"signal",reading.contact?"1":"0"},
                        {"expected_signal",expectedSignal?"1":"0"}};
                    publishMeasurement(context, step, signal);
                    append(result, std::move(signal));
                }

                stand->isd().disableYalkOutput(address);
                waitChecked(context, offSettle);
            } catch (...) {
                try { stand->isd().disableYalkOutput(address); } catch (...) {}
                throw;
            }
        }
    }
    return result;
}

ProcedureResult overload(const ScenarioStep& step, ProcedureContext& context,
                         const std::shared_ptr<hardware::StandHardware>& stand)
{
    const auto physical = addressList(argument(step, "physical_channels",
        "1-28,32-43,45-70,74-87"));
    const auto observed = addressList(argument(step, "observed_addresses",
        "1-28,32-43,45-70,74-87"));
    if (physical.empty() || observed.empty())
        throw std::invalid_argument("Не задана карта перегрузки ЯЛК");

    const unsigned samples = natural(step, "sample_count", 4);
    const unsigned baselineSettle = natural(step, "baseline_settle_ms", 1000);
    const unsigned overloadSettle = natural(step, "overload_settle_ms", 10000);
    const unsigned cleanupSettle = natural(step, "cleanup_settle_ms", 300);
    const double maxDelta = number(step, "maximum_code_delta", 2.0);
    const unsigned positiveCommon = natural(step, "positive_overload_contact", 96);
    const unsigned negativeCommon = natural(step, "negative_overload_contact", 95);

    const auto analogCode = [](unsigned channel) {
        return channel <= 10 ? 780u + (channel - 1) * 30u
                             : 1800u + (channel - 11) * 20u;
    };
    auto staircaseOn = [&] {
        for (const auto channel : physical)
            stand->isd().setAnalog(channel, analogCode(channel), true);
    };
    auto staircaseOff = [&] {
        for (auto item = physical.rbegin(); item != physical.rend(); ++item) {
            try { stand->isd().setAnalog(*item, 0, false); } catch (...) {}
        }
    };
    auto cleanup = [&](unsigned common, unsigned target) {
        try { if (target) stand->isd().setSwitch(3, target, false); } catch (...) {}
        try { if (common) stand->isd().setSwitch(3, common, false); } catch (...) {}
        staircaseOff();
        waitChecked(context, cleanupSettle);
    };

    ProcedureResult result{RunVerdict::Ok,
        "Проверена устойчивость остальных каналов ЯЛК при перегрузке ±12 В", {}};
    staircaseOff();
    staircaseOn();
    waitChecked(context, baselineSettle);
    const auto baseline = stand->yalk().readYalkSnapshot(
        samples, std::chrono::milliseconds(3000), [&context] { context.checkpoint(); });
    staircaseOff();

    const std::vector<std::pair<unsigned,std::string>> polarities{
        {positiveCommon,"+12 В"},{negativeCommon,"-12 В"}};
    unsigned impactIndex = 0;
    const unsigned impactCount = static_cast<unsigned>(physical.size() * polarities.size());
    for (const auto& polarity : polarities) {
        for (const unsigned target : physical) {
            context.checkpoint();
            ++impactIndex;
            publish(context, step, "OVERLOAD",
                "ЯЛК: перегрузка " + polarity.second + ", канал " + std::to_string(target),
                RunVerdict::NotRun,
                {{"polarity",polarity.second},{"stressed_channel",std::to_string(target)},
                 {"target_count",std::to_string(physical.size())},
                 {"impact_index",std::to_string(impactIndex)},
                 {"impact_count",std::to_string(impactCount)},
                 {"settle_ms",std::to_string(overloadSettle)}});
            try {
                staircaseOn();
                stand->isd().setSwitch(3, polarity.first, true);
                stand->isd().setAnalog(target, 0, false);
                stand->isd().setSwitch(3, target, true);
                waitChecked(context, overloadSettle);
                const auto current = stand->yalk().readYalkSnapshot(
                    samples, std::chrono::milliseconds(3000), [&context] { context.checkpoint(); });
                for (const unsigned address : observed) {
                    if (address == target) continue;
                    const double base = baseline.at(address - 1).codeMean;
                    const double now = current.at(address - 1).codeMean;
                    auto value = measurement("ubsi.yalk.overload." + polarity.second + "."
                            + std::to_string(target) + "." + std::to_string(address),
                        "ЯЛК: " + polarity.second + " на " + std::to_string(target)
                            + ", наблюдение " + std::to_string(address),
                        base, now, base - maxDelta, base + maxDelta, "код");
                    value.attributes = {{"polarity",polarity.second},
                        {"stressed_channel",std::to_string(target)},
                        {"observed_channel",std::to_string(address)},
                        {"baseline_code",std::to_string(base)},
                        {"current_code",std::to_string(now)},
                        {"delta_code",std::to_string(now-base)},
                        {"lower_delta_code",std::to_string(-maxDelta)},
                        {"upper_delta_code",std::to_string(maxDelta)},
                        {"impact_index",std::to_string(impactIndex)},
                        {"impact_count",std::to_string(impactCount)}};
                    publishMeasurement(context, step, value);
                    append(result, std::move(value));
                }
                cleanup(polarity.first, target);
            } catch (...) {
                try { cleanup(polarity.first, target); } catch (...) {}
                throw;
            }
        }
    }
    staircaseOff();
    return result;
}

ProcedureResult reference(const ScenarioStep& step, ProcedureContext& context,
                          const std::shared_ptr<hardware::StandHardware>& stand)
{
    const auto addresses = yalkAddresses(step);
    const unsigned first = addresses.front();
    const double nominal = number(step, "nominal_v", 6.2);
    const double tolerance = number(step, "tolerance_v", 0.03);
    try {
        stand->isd().setYalkVoltage(first, nominal);
        waitChecked(context, natural(step, "settle_ms", 150));
        const double volts = stand->v7().readDcVoltage();
        stand->isd().disableYalkOutput(first);
        ProcedureResult result{RunVerdict::Ok, "Проверено эталонное напряжение 6,2 В", {}};
        auto value = measurement("ubsi.reference_6v2", "Эталонное напряжение по В7",
            nominal, volts, nominal - tolerance, nominal + tolerance, "В");
        value.attributes = {{"v7_v",std::to_string(volts)},
                            {"nominal_v",std::to_string(nominal)}};
        publishMeasurement(context, step, value);
        append(result, std::move(value));
        return result;
    } catch (...) {
        try { stand->isd().disableYalkOutput(first); } catch (...) {}
        throw;
    }
}

ProcedureResult finish(const ScenarioStep& step, ProcedureContext& context,
                       const std::shared_ptr<hardware::StandHardware>& stand)
{
    stand->yalk().stop();
    waitChecked(context, natural(step, "settle_ms", 300));
    const double residual = stand->v7().readDcVoltage();
    const double maximum = number(step, "maximum_residual_voltage_v", 0.2);
    ProcedureResult result{RunVerdict::Ok, "Поток ЯЛК остановлен; воздействие снято", {}};
    auto value = measurement("ubsi.yalk.cleanup_voltage",
        "Остаточное напряжение после ЯЛК", 0.0, residual, -maximum, maximum, "В");
    value.attributes = {{"v7_v",std::to_string(residual)},
                        {"cleanup_voltage_v",std::to_string(residual)}};
    publishMeasurement(context, step, value);
    append(result, std::move(value));
    return result;
}

} // namespace

void registerYalkProcedures(ScenarioEngine& engine,
                            std::shared_ptr<hardware::StandHardware> hardware)
{
    if (!hardware) throw std::invalid_argument("StandHardware is required");
    engine.registerProcedure("stand.isd_baseline", [hardware](const auto& s, auto& c) {
        return isdBaseline(s,c,hardware); });
    engine.registerProcedure("yalk.start", [hardware](const auto& s, auto& c) {
        return start(s,c,hardware); });
    engine.registerProcedure("yalk.calibration", [hardware](const auto& s, auto& c) {
        return calibration(s,c,hardware); });
    engine.registerProcedure("yalk.initial", [hardware](const auto& s, auto& c) {
        return initial(s,c,hardware); });
    engine.registerProcedure("yalk.channels", [hardware](const auto& s, auto& c) {
        return channelSweep(s,c,hardware,false); });
    engine.registerProcedure("yalk.contacts", [hardware](const auto& s, auto& c) {
        return channelSweep(s,c,hardware,true); });
    engine.registerProcedure("yalk.overload", [hardware](const auto& s, auto& c) {
        return overload(s,c,hardware); });
    engine.registerProcedure("yalk.reference", [hardware](const auto& s, auto& c) {
        return reference(s,c,hardware); });
    engine.registerProcedure("yalk.finish", [hardware](const auto& s, auto& c) {
        return finish(s,c,hardware); });
}

} // namespace tu::procedures
