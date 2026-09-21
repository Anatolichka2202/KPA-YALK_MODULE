#include "procedures/yalk_procedures.h"
#include "procedures/yalk_contact_verdict.h"
#include "procedures/yalk_initial_verdict.h"

#include "hardware/stand_hardware.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
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

void journal(ProcedureContext& context, const ScenarioStep& step,
             const std::string& message,
             std::map<std::string, std::string> data = {})
{
    publish(context, step, "JOURNAL", message, RunVerdict::NotRun, std::move(data));
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

void publishLiveFrame(ProcedureContext& context, const ScenarioStep& step,
                      const std::vector<hardware::YalkChannelReading>& frame,
                      const std::string& message)
{
    if (frame.size() < 100) return;
    std::ostringstream values;
    values << std::setprecision(10);
    for (std::size_t index = 0; index < 100; ++index) {
        if (index) values << ',';
        values << yalkVolts(frame[index].codeMean, context);
    }
    const std::string csv = values.str();
    publish(context, step, "BACKGROUND", message, RunVerdict::NotRun,
        {{"section","YALK"},
         {"background_mean",csv},
         {"background_min",csv},
         {"background_max",csv},
         {"fresh","true"}});
}

void resetYalkRoutesForRun(const ScenarioStep& step, ProcedureContext& context,
                           const std::shared_ptr<hardware::StandHardware>& stand)
{
    const auto addresses = yalkAddresses(step);
    journal(context, step,
        "ЯЛК: адресно снимаю остаточные воздействия ИСД перед прогоном (без global type=4)");

    // После аварийного завершения новый процесс не знает activeYalk_ старого процесса.
    // Поэтому перед рабочим ЯЛК явно выключаем все 80 подтверждённых аналоговых
    // маршрутов. Это также снимает type=1 фон, который мог остаться после перегрузки.
    for (const unsigned address : addresses) {
        context.checkpoint();
        stand->isd().disableYalkOutput(address);
    }

    // Источники перегрузки type=3 выключаем адресно. Целевые type=3 здесь не
    // перебираем: для обычного ЯЛК достаточно гарантированно снять общие ±12 В,
    // а спорную методику перегрузки этим восстановлением не меняем.
    stand->isd().setSwitch(3, 95, false);
    stand->isd().setSwitch(3, 96, false);
    waitChecked(context, natural(step, "preclean_settle_ms", 300));
    journal(context, step, "ЯЛК: 80 аналоговых маршрутов и источники ±12 В сняты");
}

ProcedureResult start(const ScenarioStep& step, ProcedureContext& context,
                      const std::shared_ptr<hardware::StandHardware>& stand)
{
    journal(context, step, "ROKT: запускаю рабочий поток ЯЛК reference204");
    const bool ready = stand->yalk().startYalk(
        std::chrono::milliseconds(natural(step, "configure_settle_ms", 500)),
        std::chrono::milliseconds(natural(step, "timeout_ms", 3000)),
        [&context] { context.checkpoint(); });
    if (!ready) return {RunVerdict::Fail, "Не получен reference204 кадр ЯЛК", {}};
    journal(context, step, "ROKT: reference204 получен, поток ЯЛК работает");
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
        journal(context, step, "Калибровка ЯЛК: подаю 6,2 В через первый рабочий канал ИСД");
        stand->isd().setYalkVoltage(first, fullVoltage);
        waitChecked(context, settle);
        const double reference = stand->v7().readDcVoltage();
        const auto frame = stand->yalk().readYalkSnapshot(
            samples, std::chrono::milliseconds(3000), [&context] { context.checkpoint(); });
        stand->isd().disableYalkOutput(first);

        const double zero = frame.at(96).codeMean;
        const double full = frame.at(98).codeMean;
        if (!(full > zero) || reference < 5.5 || reference > 6.8)
            throw std::runtime_error("Недостоверная калибровка ЯЛК 97/99 или напряжение В7");

        context.state["yalk.zero_code"] = std::to_string(zero);
        context.state["yalk.full_code"] = std::to_string(full);
        context.state["yalk.full_voltage"] = std::to_string(fullVoltage);
        context.state["yalk.calibration_reference_v"] = std::to_string(reference);
        publishLiveFrame(context, step, frame, "Свежий reference204 после калибровки");

        ProcedureResult result{RunVerdict::Ok, "Снята калибровка ЯЛК по адресам 97/99", {}};
        auto value = measurement("ubsi.yalk.calibration", "Опорное воздействие калибровки ЯЛК",
                                 6.2, reference, 5.5, 6.8, "В");
        value.attributes = {{"zero_address","97"},{"full_address","99"},
                            {"zero_code",std::to_string(zero)},
                            {"full_code",std::to_string(full)},
                            {"scale_voltage_v",std::to_string(fullVoltage)},
                            {"v7_v",std::to_string(reference)},
                            {"stimulus_command_v",std::to_string(fullVoltage)}};
        publishMeasurement(context, step, value);
        append(result, std::move(value));
        journal(context, step, "Калибровка ЯЛК завершена: 97=" + std::to_string(zero)
            + ", 99=" + std::to_string(full) + ", В7=" + std::to_string(reference) + " В");
        return result;
    } catch (...) {
        try { stand->isd().disableYalkOutput(first); } catch (...) {}
        stand->isd().safeStop();
        throw;
    }
}

ProcedureResult initial(const ScenarioStep& step, ProcedureContext& context,
                        const std::shared_ptr<hardware::StandHardware>& stand)
{
    const auto addresses = yalkAddresses(step);
    journal(context, step, "ЯЛК: читаю исходное состояние всех 80 рабочих адресов");
    const auto frame = stand->yalk().readYalkSnapshot(
        natural(step, "sample_count", 4), std::chrono::milliseconds(3000),
        [&context] { context.checkpoint(); });
    publishLiveFrame(context, step, frame, "Исходный reference204 ЯЛК");
    ProcedureResult result{RunVerdict::Ok, "Проверено отключённое состояние 80 входов ЯЛК", {}};

    for (std::size_t channelIndex = 0; channelIndex < addresses.size(); ++channelIndex) {
        context.checkpoint();
        const unsigned address = addresses[channelIndex];
        const auto& reading = frame.at(address - 1);
        const double volts = yalkVolts(reading.codeMean, context);

        auto analog = measurement("ubsi.yalk.initial." + std::to_string(address),
            "ЯЛК адрес " + std::to_string(address) + ": обрыв", 0.0, volts,
            -number(step,"full_scale_v",6.2), -1e-12, "В");
        if (!detail::yalkOpenCircuitIsNormal(volts)) {
            analog.verdict = RunVerdict::Fail;
            analog.message = "При обрыве значение должно быть ниже 0 В";
        }
        analog.attributes = {{"ulk_address",std::to_string(address)},
                             {"raw",std::to_string(reading.rawMean)},
                             {"analog_code",std::to_string(reading.codeMean)},
                             {"yalk_v",std::to_string(volts)},
                             {"signal",reading.contact?"1":"0"}};

        auto eventData = analog.attributes;
        eventData["channel_index"] = std::to_string(channelIndex + 1);
        eventData["channel_count"] = std::to_string(addresses.size());
        // The contact bit is diagnostic for an open input.  TU 1.1.4.10
        // accepts or rejects this state solely by UyalK < 0 V.
        eventData["signal_check"] = "diagnostic_only";
        eventData["analog_ok"] = analog.verdict == RunVerdict::Ok ? "1" : "0";
        publish(context, step, "YALK_INITIAL", analog.title, analog.verdict, std::move(eventData));

        append(result, std::move(analog));
    }
    return result;
}

ProcedureResult channelSweep(const ScenarioStep& step, ProcedureContext& context,
                             const std::shared_ptr<hardware::StandHardware>& stand,
                             bool thresholds)
{
    const auto addresses = yalkAddresses(step);
    // trace_addresses is a diagnostic subset of the confirmed 80-address map.
    // The acceptance scenario omits it and therefore retains the full sweep.
    auto testedAddresses = addressList(argument(step, "trace_addresses"));
    if (testedAddresses.empty()) testedAddresses = addresses;
    for (const unsigned address : testedAddresses) {
        if (std::find(addresses.begin(), addresses.end(), address) == addresses.end())
            throw std::invalid_argument("Адрес выборочной проверки отсутствует в карте ЯЛК");
    }
    const auto points = numbers(step, thresholds ? "contact_points_v" : "point_volts");
    const auto expected = thresholds ? numbers(step, "signal_expectations") : std::vector<double>{};
    if (points.empty() || (thresholds && expected.size() != points.size()))
        throw std::invalid_argument("Не заполнены точки ЯЛК");
    const auto contactPolicy = thresholds
        ? detail::yalkContactVerdictPolicy(argument(step, "verdict_policy", "strict"))
        : detail::YalkContactVerdictPolicy::Strict;

    const unsigned samples = natural(step, "sample_count", 16);
    const unsigned settle = natural(step, "settle_ms", 150);
    const unsigned offSettle = natural(step, "channel_off_settle_ms", 1000);
    const double fullScale = number(step, "full_scale_v", 6.2);
    const double tolerance = fullScale
        * number(step, "tolerance_percent_fs", 0.5) / 100.0;
    ProcedureResult result{RunVerdict::Ok,
        thresholds ? "Проверены контактные пороги " + std::to_string(testedAddresses.size())
                         + " адресов ЯЛК"
                   : "Проверены аналоговые каналы " + std::to_string(testedAddresses.size())
                         + " адресов ЯЛК", {}};

    // Минимальная поставка проходила канал-major: один физический канал ИСД,
    // все его точки подряд, затем канал снимается и только после этого берётся
    // следующий. Не делаем point-major 0 В по всем 80, затем 3,1 и т.д.
    for (std::size_t channelIndex = 0; channelIndex < testedAddresses.size(); ++channelIndex) {
        context.checkpoint();
        const unsigned address = testedAddresses[channelIndex];
        bool outputEnabled = false;
        try {
            journal(context, step,
                std::string(thresholds ? "Контактный признак" : "Аналоговый вход")
                    + ": канал " + std::to_string(channelIndex + 1) + "/"
                    + std::to_string(testedAddresses.size()) + ", адрес "
                    + std::to_string(address));

            for (std::size_t pointIndex = 0; pointIndex < points.size(); ++pointIndex) {
                context.checkpoint();
                const double command = points[pointIndex];
                publish(context, step, "YALK_POINT",
                    "ЯЛК адрес " + std::to_string(address) + ": подаю "
                        + std::to_string(command) + " В",
                    RunVerdict::NotRun,
                    {{"point_index",std::to_string(pointIndex + 1)},
                     {"point_count",std::to_string(points.size())},
                     {"channel_index",std::to_string(channelIndex + 1)},
                     {"channel_count",std::to_string(testedAddresses.size())},
                     {"ulk_address",std::to_string(address)},
                     {"command_v",std::to_string(command)}});
                journal(context, step, "ИСД: канал " + std::to_string(address)
                    + " -> " + std::to_string(command) + " В; ожидаю установление");

                stand->isd().setYalkVoltage(address, command);
                outputEnabled = true;
                waitChecked(context, settle);
                const double reference = stand->v7().readDcVoltage();
                const auto frame = stand->yalk().readYalkSnapshot(
                    samples, std::chrono::milliseconds(3000),
                    [&context] { context.checkpoint(); });
                publishLiveFrame(context, step, frame,
                    "Свежий reference204 · адрес " + std::to_string(address)
                        + " · воздействие " + std::to_string(command) + " В");

                const auto& reading = frame.at(address - 1);
                const double volts = yalkVolts(reading.codeMean, context);
                const double absolute = std::abs(volts - reference);
                const double reduced = absolute / fullScale * 100.0;

                if (!thresholds) {
                    auto analog = measurement("ubsi.yalk.channel." + std::to_string(address)
                            + "." + std::to_string(pointIndex),
                        "ЯЛК адрес " + std::to_string(address) + " · "
                            + std::to_string(command) + " В",
                        reference, volts, reference - tolerance, reference + tolerance, "В");
                    analog.attributes = {{"section","YALK"},
                        {"channel",std::to_string(channelIndex + 1)},
                        {"channel_index",std::to_string(channelIndex + 1)},
                        {"channel_count",std::to_string(testedAddresses.size())},
                        {"ulk_address",std::to_string(address)},
                        {"command_v",std::to_string(command)},
                        {"point_index",std::to_string(pointIndex + 1)},
                        {"point_count",std::to_string(points.size())},
                        {"scan_order","channel_major"},
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
                    journal(context, step, "В7=" + std::to_string(reference)
                        + " В; ЯЛК=" + std::to_string(volts) + " В");
                } else {
                    const bool expectedSignal = expected[pointIndex] >= 0.5;
                    const auto decision = detail::yalkContactVerdict(
                        contactPolicy, expectedSignal, reading.contact);
                    auto signal = measurement("ubsi.yalk.signal." + std::to_string(address)
                            + "." + std::to_string(pointIndex),
                        "ЯЛК адрес " + std::to_string(address) + ": контакт при "
                            + std::to_string(command) + " В",
                        expectedSignal ? 1.0 : 0.0,
                        reading.contact ? 1.0 : 0.0,
                        expectedSignal ? 1.0 : 0.0,
                        expectedSignal ? 1.0 : 0.0, "лог.");
                    signal.verdict = decision.acceptanceVerdict;
                    signal.message = signal.verdict == RunVerdict::Ok
                        ? "Норма" : "Значение вне допуска";
                    signal.attributes = {{"section","YALK"},
                        {"channel",std::to_string(channelIndex + 1)},
                        {"channel_index",std::to_string(channelIndex + 1)},
                        {"channel_count",std::to_string(testedAddresses.size())},
                        {"ulk_address",std::to_string(address)},
                        {"command_v",std::to_string(command)},
                        {"point_index",std::to_string(pointIndex + 1)},
                        {"point_count",std::to_string(points.size())},
                        {"scan_order","channel_major"},
                        {"v7_v",std::to_string(reference)},
                        {"yalk_v",std::to_string(volts)},
                        {"raw",std::to_string(reading.rawMean)},
                        {"analog_code",std::to_string(reading.codeMean)},
                        {"signal",reading.contact?"1":"0"},
                        {"raw_signal",decision.rawSignal?"1":"0"},
                        {"expected_signal",decision.expectedSignal?"1":"0"},
                        {"raw_match",decision.rawMatch?"true":"false"},
                        {"formal_override",decision.formalOverride?"true":"false"}};
                    publishMeasurement(context, step, signal);
                    append(result, std::move(signal));
                    journal(context, step, "В7=" + std::to_string(reference)
                        + " В; признак=" + (reading.contact ? std::string("1") : std::string("0"))
                        + "; ожидается=" + (expectedSignal ? std::string("1") : std::string("0")));
                }
            }

            journal(context, step, "ИСД: снимаю воздействие с адреса " + std::to_string(address));
            stand->isd().disableYalkOutput(address);
            outputEnabled = false;
            waitChecked(context, offSettle);
        } catch (...) {
            if (outputEnabled) {
                try { stand->isd().disableYalkOutput(address); } catch (...) {}
            }
            stand->isd().safeStop();
            throw;
        }
    }
    return result;
}

ProcedureResult combinedSweep(const ScenarioStep& step, ProcedureContext& context,
                              const std::shared_ptr<hardware::StandHardware>& stand)
{
    const auto addresses = yalkAddresses(step);
    const auto points = numbers(step, "combined_points_v");
    const auto analogPoints = numbers(step, "point_volts");
    const auto contactPoints = numbers(step, "contact_points_v");
    const auto expected = numbers(step, "signal_expectations");
    if (points.empty() || expected.size() != contactPoints.size())
        throw std::invalid_argument("Не заполнены объединённые точки ЯЛК");
    const auto includes = [](const std::vector<double>& values, double value) {
        return std::any_of(values.begin(), values.end(), [value](double candidate) {
            return std::abs(candidate - value) < 1e-9;
        });
    };
    for (const double point : analogPoints)
        if (!includes(points, point)) throw std::invalid_argument("Аналоговая точка отсутствует в порядке ЯЛК");
    for (const double point : contactPoints)
        if (!includes(points, point)) throw std::invalid_argument("Контактная точка отсутствует в порядке ЯЛК");

    const auto policy = detail::yalkContactVerdictPolicy(argument(step, "verdict_policy", "strict"));
    const unsigned samples = natural(step, "sample_count", 16);
    const unsigned settle = natural(step, "settle_ms", 150);
    const unsigned contact24Settle = natural(step, "contact_2_4_settle_ms", settle);
    const double fullScale = number(step, "full_scale_v", 6.2);
    const double tolerance = fullScale * number(step, "tolerance_percent_fs", 0.5) / 100.0;
    ProcedureResult result{RunVerdict::Ok, "Проверены аналоговые и контактные точки 80 адресов ЯЛК", {}};

    for (std::size_t channelIndex = 0; channelIndex < addresses.size(); ++channelIndex) {
        const unsigned address = addresses[channelIndex];
        bool outputEnabled = false;
        try {
            std::size_t analogIndex = 0;
            std::size_t contactIndex = 0;
            journal(context, step, "ЯЛК: объединённый проход канала " + std::to_string(channelIndex + 1)
                + "/" + std::to_string(addresses.size()) + ", адрес " + std::to_string(address));
            for (std::size_t sequenceIndex = 0; sequenceIndex < points.size(); ++sequenceIndex) {
                context.checkpoint();
                const double command = points[sequenceIndex];
                const bool analogPoint = includes(analogPoints, command);
                const bool contactPoint = includes(contactPoints, command);
                const unsigned pointSettle = contactPoint && std::abs(command - 2.4) < 1e-9
                    ? contact24Settle : settle;
                stand->isd().setYalkVoltage(address, command);
                outputEnabled = true;
                waitChecked(context, pointSettle);
                const double reference = stand->v7().readDcVoltage();
                const auto frame = stand->yalk().readYalkSnapshot(samples, std::chrono::milliseconds(3000),
                    [&context] { context.checkpoint(); });
                publishLiveFrame(context, step, frame, "Свежий reference204 · адрес "
                    + std::to_string(address) + " · " + std::to_string(command) + " В");
                const auto& reading = frame.at(address - 1);
                const double volts = yalkVolts(reading.codeMean, context);
                const double absolute = std::abs(volts - reference);
                const auto attributes = [&](std::size_t index, std::size_t count) {
                    return std::map<std::string,std::string>{{"section","YALK"},
                        {"channel_index",std::to_string(channelIndex + 1)}, {"channel_count",std::to_string(addresses.size())},
                        {"ulk_address",std::to_string(address)}, {"command_v",std::to_string(command)},
                        {"point_index",std::to_string(index)}, {"point_count",std::to_string(count)},
                        {"sequence_index",std::to_string(sequenceIndex + 1)}, {"sequence_count",std::to_string(points.size())},
                        {"scan_order","channel_major_combined"}, {"v7_v",std::to_string(reference)},
                        {"yalk_v",std::to_string(volts)}, {"raw",std::to_string(reading.rawMean)},
                        {"analog_code",std::to_string(reading.codeMean)}, {"signal",reading.contact ? "1" : "0"}};
                };
                if (analogPoint) {
                    ++analogIndex;
                    auto analog = measurement("ubsi.yalk.channel." + std::to_string(address) + "." + std::to_string(analogIndex - 1),
                        "ЯЛК адрес " + std::to_string(address) + " · " + std::to_string(command) + " В",
                        reference, volts, reference - tolerance, reference + tolerance, "В");
                    analog.attributes = attributes(analogIndex, analogPoints.size());
                    analog.attributes["absolute_error_v"] = std::to_string(absolute);
                    analog.attributes["reduced_error_percent"] = std::to_string(absolute / fullScale * 100.0);
                    publishMeasurement(context, step, analog);
                    append(result, std::move(analog));
                }
                if (contactPoint) {
                    const auto position = std::find_if(contactPoints.begin(), contactPoints.end(), [command](double candidate) {
                        return std::abs(candidate - command) < 1e-9;
                    });
                    const std::size_t expectedIndex = static_cast<std::size_t>(position - contactPoints.begin());
                    ++contactIndex;
                    const bool expectedSignal = expected[expectedIndex] >= 0.5;
                    const auto decision = detail::yalkContactVerdict(policy, expectedSignal, reading.contact);
                    auto signal = measurement("ubsi.yalk.signal." + std::to_string(address) + "." + std::to_string(contactIndex - 1),
                        "ЯЛК адрес " + std::to_string(address) + ": контакт при " + std::to_string(command) + " В",
                        expectedSignal ? 1.0 : 0.0, reading.contact ? 1.0 : 0.0,
                        expectedSignal ? 1.0 : 0.0, expectedSignal ? 1.0 : 0.0, "лог.");
                    signal.verdict = decision.acceptanceVerdict;
                    signal.message = signal.verdict == RunVerdict::Ok ? "Норма" : "Значение вне допуска";
                    signal.attributes = attributes(contactIndex, contactPoints.size());
                    signal.attributes["raw_signal"] = decision.rawSignal ? "1" : "0";
                    signal.attributes["expected_signal"] = decision.expectedSignal ? "1" : "0";
                    signal.attributes["raw_match"] = decision.rawMatch ? "true" : "false";
                    signal.attributes["formal_override"] = decision.formalOverride ? "true" : "false";
                    publishMeasurement(context, step, signal);
                    append(result, std::move(signal));
                }
            }
            journal(context, step, "ИСД: снимаю воздействие с адреса " + std::to_string(address)
                + "; встроенная двухфазная пауза ИСД сохранена");
            stand->isd().disableYalkOutput(address);
            outputEnabled = false;
        } catch (...) {
            if (outputEnabled) { try { stand->isd().disableYalkOutput(address); } catch (...) {} }
            stand->isd().safeStop();
            throw;
        }
    }
    return result;
}

ProcedureResult overload(const ScenarioStep& step, ProcedureContext& context,
                         const std::shared_ptr<hardware::StandHardware>& stand)
{
    const auto physical = addressList(argument(step, "physical_channels",
        "1-28,32-43,45-70,74-87"));
    const auto stressed = addressList(argument(step, "stressed_channels",
        argument(step, "physical_channels", "1-28,32-43,45-70,74-87")));
    const auto observed = addressList(argument(step, "observed_addresses",
        "1-28,32-43,45-70,74-87"));
    if (physical.size() != 80 || observed.size() != 80 || stressed.empty())
        throw std::invalid_argument("Перегрузка ЯЛК должна использовать безопасную карту 80 каналов");
    for (const unsigned target : stressed) {
        if (std::find(physical.begin(), physical.end(), target) == physical.end())
            throw std::invalid_argument("Канал выборочной перегрузки отсутствует в карте ЯЛК");
    }

    const unsigned samples = natural(step, "sample_count", 4);
    const unsigned baselineSettle = natural(step, "baseline_settle_ms", 1000);
    const unsigned dacOffSettle = natural(step, "dac_off_settle_ms", 300);
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
    auto sourceOff = [&](unsigned common) {
        if (!common) return;
        try { stand->isd().setSwitch(3, common, false); } catch (...) {}
    };
    auto impactOff = [&](unsigned common, unsigned target, bool restoreBackground) {
        try { if (target) stand->isd().setSwitch(3, target, false); } catch (...) {}
        sourceOff(common);
        if (restoreBackground && target) {
            stand->isd().setAnalog(target, analogCode(target), true);
        }
        waitChecked(context, cleanupSettle);
    };

    ProcedureResult result{RunVerdict::Ok,
        stressed.size() == physical.size()
            ? "Проверена устойчивость остальных каналов ЯЛК при перегрузке ±12 В"
            : "Проверена устойчивость 80 адресов ЯЛК при выборочной перегрузке ±12 В на "
                + std::to_string(stressed.size())
                + (stressed.size() == 1 ? " канале" : " каналах"), {}};

    // Фон формируется один раз. Начальный all-off выполняется отдельным
    // шагом перед ЯЛК; здесь не повторяем 80 адресных выключений. Для каждого
    // воздействия отключается только ЦАП цели; 79 остальных фоновых ЦАП
    // остаются включены. Лишь затем к цели подключается общий источник ±12 В.
    // Исключённые 29/30/31/44/71/72/73/88 не затрагиваются.
    try {
        sourceOff(positiveCommon);
        sourceOff(negativeCommon);
        journal(context, step, "Перегрузка: формирую безопасный 80-канальный пилообразный фон ИСД");
        staircaseOn();
        waitChecked(context, baselineSettle);
        const auto baseline = stand->yalk().readYalkSnapshot(
            samples, std::chrono::milliseconds(3000), [&context] { context.checkpoint(); });
        publishLiveFrame(context, step, baseline, "Baseline ЯЛК перед перегрузкой");
        journal(context, step, "Перегрузка: baseline reference204 снят; начинаю ±12 В");

        const std::vector<std::pair<unsigned,std::string>> polarities{
            {positiveCommon,"+12 В"},{negativeCommon,"-12 В"}};
        unsigned impactIndex = 0;
        const unsigned impactCount = static_cast<unsigned>(stressed.size() * polarities.size());

        for (const auto& polarity : polarities) {
            for (const unsigned target : stressed) {
                context.checkpoint();
                ++impactIndex;
                publish(context, step, "OVERLOAD",
                    "ЯЛК: перегрузка " + polarity.second + ", канал " + std::to_string(target),
                    RunVerdict::NotRun,
                    {{"polarity",polarity.second},{"stressed_channel",std::to_string(target)},
                     {"target_count",std::to_string(stressed.size())},
                     {"observed_count",std::to_string(observed.size())},
                     {"coverage",stressed.size() == physical.size() ? "full" : "selective"},
                     {"impact_index",std::to_string(impactIndex)},
                     {"impact_count",std::to_string(impactCount)},
                     {"settle_ms",std::to_string(overloadSettle)}});

                bool targetConnected = false;
                bool targetDacRemoved = false;
                try {
                    journal(context, step, "Перегрузка " + polarity.second + " · "
                        + std::to_string(impactIndex) + "/" + std::to_string(impactCount)
                        + ": канал " + std::to_string(target)
                        + " — отключаю ЦАП целевого канала; остальной 79-канальный фон остаётся включён");

                    // Методика: target DAC off -> пауза -> +12/-12 на цель.
                    // Общий источник включается только после снятия ЦАП цели.
                    stand->isd().setAnalog(target, 0, false);
                    targetDacRemoved = true;
                    waitChecked(context, dacOffSettle);

                    journal(context, step, "Перегрузка: подключаю источник type=3/"
                        + std::to_string(polarity.first) + " к каналу "
                        + std::to_string(target));
                    stand->isd().setSwitch(3, polarity.first, true);
                    stand->isd().setSwitch(3, target, true);
                    targetConnected = true;

                    journal(context, step, "Перегрузка: выдержка "
                        + std::to_string(overloadSettle / 1000.0) + " с на канале "
                        + std::to_string(target));
                    waitChecked(context, overloadSettle);

                    const auto current = stand->yalk().readYalkSnapshot(
                        samples, std::chrono::milliseconds(3000), [&context] { context.checkpoint(); });
                    publishLiveFrame(context, step, current,
                        "reference204 при " + polarity.second + " на канале "
                            + std::to_string(target));

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

                    impactOff(polarity.first, target, true);
                    targetConnected = false;
                    targetDacRemoved = false;
                    journal(context, step, "Перегрузка: канал " + std::to_string(target)
                        + " снят, пилообразный фон восстановлен");
                } catch (...) {
                    if (targetConnected) {
                        try { stand->isd().setSwitch(3, target, false); } catch (...) {}
                    }
                    sourceOff(polarity.first);
                    if (targetDacRemoved) {
                        try { stand->isd().setAnalog(target, analogCode(target), true); } catch (...) {}
                    }
                    staircaseOff();
                    stand->isd().safeStop();
                    throw;
                }
            }
        }

        journal(context, step, "Перегрузка: все " + std::to_string(impactCount)
            + " воздействий завершены, снимаю пилообразный фон");
        sourceOff(positiveCommon);
        sourceOff(negativeCommon);
        staircaseOff();
        return result;
    } catch (...) {
        sourceOff(positiveCommon);
        sourceOff(negativeCommon);
        staircaseOff();
        stand->isd().safeStop();
        throw;
    }
}

ProcedureResult reference(const ScenarioStep& step, ProcedureContext& context,
                          const std::shared_ptr<hardware::StandHardware>& stand)
{
    const double nominal = number(step, "nominal_v", 6.2);
    const double tolerance = number(step, "tolerance_v", 0.03);
    context.checkpoint();
    journal(context, step, "ЯЛК: контролирую служебный эталон полной шкалы, адрес 99");
    const auto frame = stand->yalk().readYalkSnapshot(
        natural(step, "sample_count", 16), std::chrono::milliseconds(3000),
        [&context] { context.checkpoint(); });
    publishLiveFrame(context, step, frame, "reference204 со служебным эталоном ЯЛК");
    const auto& fullScaleReference = frame.at(98);
    const double volts = yalkVolts(fullScaleReference.codeMean, context);

    ProcedureResult result{RunVerdict::Ok,
        "Проверен служебный эталон полной шкалы ЯЛК по адресу 99", {}};
    auto value = measurement("ubsi.reference_6v2", "Эталон полной шкалы ЯЛК, адрес 99",
        nominal, volts, nominal - tolerance, nominal + tolerance, "В");
    value.attributes = {{"ulk_address","99"},
                        {"raw",std::to_string(fullScaleReference.rawMean)},
                        {"analog_code",std::to_string(fullScaleReference.codeMean)},
                        {"yalk_v",std::to_string(volts)},
                        {"nominal_v",std::to_string(nominal)},
                        {"source","yalk_internal_full_scale_reference"}};
    publishMeasurement(context, step, value);
    append(result, std::move(value));
    journal(context, step, "ЯЛК: эталон адреса 99=" + std::to_string(volts) + " В");
    return result;
}

ProcedureResult finish(const ScenarioStep& step, ProcedureContext& context,
                       const std::shared_ptr<hardware::StandHardware>& stand)
{
    journal(context, step, "ЯЛК: снимаю активные воздействия и останавливаю поток");
    stand->isd().safeStop();
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
    journal(context, step, "ЯЛК: остаточное напряжение В7=" + std::to_string(residual) + " В");
    return result;
}

} // namespace

void registerYalkProcedures(ScenarioEngine& engine,
                            std::shared_ptr<hardware::StandHardware> hardware)
{
    if (!hardware) throw std::invalid_argument("StandHardware is required");
    engine.registerProcedure("yalk.addressed_reset", [hardware](const auto& s, auto& c) {
        resetYalkRoutesForRun(s,c,hardware);
        return ProcedureResult{RunVerdict::Ok,
            "Адресно сняты воздействия с 80 каналов ЯЛК и источников ±12 В", {}};
    });
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
    engine.registerProcedure("yalk.combined", [hardware](const auto& s, auto& c) {
        return combinedSweep(s,c,hardware); });
    engine.registerProcedure("yalk.overload", [hardware](const auto& s, auto& c) {
        return overload(s,c,hardware); });
    engine.registerProcedure("yalk.reference", [hardware](const auto& s, auto& c) {
        return reference(s,c,hardware); });
    engine.registerProcedure("yalk.finish", [hardware](const auto& s, auto& c) {
        return finish(s,c,hardware); });
}

} // namespace tu::procedures
