#include "procedures/ytp_procedures.h"

#include "hardware/stand_hardware.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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
    MeasurementResult value;
    value.parameterKey = std::move(key);
    value.title = std::move(title);
    value.reference = reference;
    value.measured = measured;
    value.lowerLimit = lower;
    value.upperLimit = upper;
    value.unit = std::move(unit);
    value.verdict = limit(measured, lower, upper);
    value.message = value.verdict == RunVerdict::Ok ? "Норма" : "Значение вне допуска";
    return value;
}

void append(ProcedureResult& result, MeasurementResult value)
{
    result.verdict = combineVerdicts(result.verdict, value.verdict);
    result.measurements.push_back(std::move(value));
}

ProcedureResult start(const ScenarioStep& step, ProcedureContext& context,
                      const std::shared_ptr<hardware::StandHardware>& stand)
{
    const unsigned endpoint = natural(step, "ytp_endpoint", 1);
    const bool ready = stand->yalk().startYtp(
        endpoint,
        std::chrono::milliseconds(natural(step, "configure_settle_ms", 500)),
        std::chrono::milliseconds(natural(step, "stream_settle_ms", 1000)),
        std::chrono::milliseconds(natural(step, "timeout_ms", 3000)),
        [&context] { context.checkpoint(); });
    if (!ready) return {RunVerdict::Fail, "Не получен ROKT кадр ЯТП 68 байт", {}};
    context.state["ytp.protocol"] = "rokt_ytp68";
    return {RunVerdict::Ok,
        "Запущен ЯТП через адаптер: ROKT 0A 02 00 01 00, принимаются кадры 68 байт", {}};
}

ProcedureResult calibration(const ScenarioStep& step, ProcedureContext& context,
                            const std::shared_ptr<hardware::StandHardware>& stand)
{
    const auto snapshot = stand->yalk().readYtpSnapshot(
        natural(step, "sample_count", 16), std::chrono::milliseconds(3000),
        [&context] { context.checkpoint(); });
    const double zero = snapshot.calibration32; // подтверждённая карта: слово 32 = 0 Ом
    const double full = snapshot.calibration31; // слово 31 = 240 Ом
    if (zero == 32768.0 || full == 32768.0 || !(full > zero))
        throw std::runtime_error("Недостоверная калибровка ЯТП по словам 32/31");
    context.state["ytp.calibration_zero_raw"] = std::to_string(zero);
    context.state["ytp.calibration_full_raw"] = std::to_string(full);

    ProcedureResult result{RunVerdict::Ok,
        "Калибровка ЯТП подтверждена: слово 32 — 0 Ом, слово 31 — 240 Ом", {}};
    auto value = measurement("ubsi.ytp.calibration_span", "Размах калибровки ЯТП",
        1.0, full > zero ? 1.0 : 0.0, 1.0, 1.0, "лог.");
    value.attributes = {{"zero_word","32"},{"full_word","31"},
                        {"zero_raw",std::to_string(zero)},
                        {"full_raw",std::to_string(full)},
                        {"valid_word_count",std::to_string(snapshot.validWordCount)}};
    append(result, std::move(value));
    return result;
}

ProcedureResult channels(const ScenarioStep& step, ProcedureContext& context,
                         const std::shared_ptr<hardware::StandHardware>& stand,
                         const OperatorConfirm& confirm)
{
    const unsigned channelCount = natural(step, "channel_count", 30);
    if (channelCount != 30) throw std::invalid_argument("ЯТП содержит 30 измерительных каналов");
    const auto points = numbers(step, "resistance_points_ohm");
    if (points.empty()) throw std::invalid_argument("Не заданы точки сопротивления ЯТП");
    const double fullScale = number(step, "full_scale_ohm", 240.0);
    const double tolerance = fullScale * number(step, "tolerance_percent_fs", 0.5) / 100.0;
    const unsigned samples = natural(step, "sample_count", 16);
    const unsigned settle = natural(step, "settle_ms", 1500);

    const auto z = context.state.find("ytp.calibration_zero_raw");
    const auto f = context.state.find("ytp.calibration_full_raw");
    if (z == context.state.end() || f == context.state.end())
        throw std::runtime_error("Нет калибровки ЯТП");
    const double zero = std::stod(z->second), full = std::stod(f->second);
    if (!(full > zero)) throw std::runtime_error("Неверная калибровка ЯТП");

    ProcedureResult result{RunVerdict::Ok,
        "Проверены 30 каналов ЯТП при 0 / 120 / 240 Ом", {}};
    for (std::size_t pointIndex = 0; pointIndex < points.size(); ++pointIndex) {
        context.checkpoint();
        const std::string title = "ЯТП · магазин сопротивлений";
        const std::string prompt = "Установите Р4831 = " + std::to_string(points[pointIndex])
            + " Ом и подтвердите продолжение";
        if (!confirm || !confirm(title, prompt)) {
            return {RunVerdict::Incomplete,
                "Оператор не подтвердил установку Р4831; оставшиеся точки ЯТП не выполнялись", {}};
        }
        waitChecked(context, settle);
        const auto snapshot = stand->yalk().readYtpSnapshot(
            samples, std::chrono::milliseconds(3000), [&context] { context.checkpoint(); });

        if (context.eventSink) context.eventSink({
            std::chrono::system_clock::now(), step.id, "YTP_POINT",
            "ЯТП: " + std::to_string(points[pointIndex]) + " Ом",
            RunVerdict::NotRun,
            {{"point_index",std::to_string(pointIndex)},
             {"point_count",std::to_string(points.size())},
             {"resistance_ohm",std::to_string(points[pointIndex])}}});

        for (unsigned channel = 0; channel < channelCount; ++channel) {
            context.checkpoint();
            const double raw = snapshot.channels[channel];
            const double ohms = (raw - zero) * fullScale / (full - zero);
            auto value = measurement("ubsi.ytp.channel." + std::to_string(channel + 1)
                    + "." + std::to_string(pointIndex),
                "ЯТП канал " + std::to_string(channel + 1) + " · "
                    + std::to_string(points[pointIndex]) + " Ом",
                points[pointIndex], ohms,
                points[pointIndex] - tolerance, points[pointIndex] + tolerance, "Ом");
            value.attributes = {{"section","YTP"},
                {"channel",std::to_string(channel + 1)},
                {"reference_ohm",std::to_string(points[pointIndex])},
                {"measured_ohm",std::to_string(ohms)},
                {"raw",std::to_string(raw)},
                {"zero_raw",std::to_string(zero)},
                {"full_raw",std::to_string(full)}};
            append(result, std::move(value));
        }
    }
    return result;
}

ProcedureResult finish(const ScenarioStep&, ProcedureContext& context,
                       const std::shared_ptr<hardware::StandHardware>& stand)
{
    context.checkpoint();
    stand->yalk().stop();
    return {RunVerdict::Ok, "Поток ЯТП остановлен", {}};
}

} // namespace

void registerYtpProcedures(ScenarioEngine& engine,
                           std::shared_ptr<hardware::StandHardware> hardware,
                           OperatorConfirm operatorConfirm)
{
    if (!hardware) throw std::invalid_argument("StandHardware is required");
    engine.registerProcedure("ytp.start", [hardware](const auto& s, auto& c) {
        return start(s,c,hardware); });
    engine.registerProcedure("ytp.calibration", [hardware](const auto& s, auto& c) {
        return calibration(s,c,hardware); });
    engine.registerProcedure("ytp.channels", [hardware,operatorConfirm](const auto& s, auto& c) {
        return channels(s,c,hardware,operatorConfirm); });
    engine.registerProcedure("ytp.finish", [hardware](const auto& s, auto& c) {
        return finish(s,c,hardware); });
}

} // namespace tu::procedures
