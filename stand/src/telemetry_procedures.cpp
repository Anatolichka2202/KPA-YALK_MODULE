#include "orbita_stand/telemetry_procedures.h"

#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>

namespace orbita::stand {
namespace {

std::map<std::string, std::string> values(const std::string& response)
{
    std::map<std::string, std::string> result;
    std::stringstream stream(response);
    std::string line;
    while (std::getline(stream, line)) {
        const auto equal = line.find('=');
        if (equal != std::string::npos && equal != 0) {
            result[line.substr(0, equal)] = line.substr(equal + 1);
        }
    }
    return result;
}

double requiredNumber(
    const std::map<std::string, std::string>& response, const std::string& key)
{
    const auto iterator = response.find(key);
    if (iterator == response.end()) {
        throw std::runtime_error("Источник Орбита не вернул поле " + key);
    }
    return std::stod(iterator->second);
}

bool flag(const ScenarioNode& node, const std::string& key, bool fallback = false)
{
    const auto iterator = node.arguments.find(key);
    if (iterator == node.arguments.end()) return fallback;
    return iterator->second == "true" || iterator->second == "1"
        || iterator->second == "yes";
}

double argumentNumber(
    const ScenarioNode& node, const std::string& key, double fallback)
{
    const auto iterator = node.arguments.find(key);
    return iterator == node.arguments.end() ? fallback
                                            : std::stod(iterator->second);
}

MeasurementResult checked(
    std::string key, std::string title, double value, double lower, double upper,
    std::string unit)
{
    MeasurementResult measurement;
    measurement.parameterKey = std::move(key);
    measurement.title = std::move(title);
    measurement.measured = value;
    measurement.lowerLimit = lower;
    measurement.upperLimit = upper;
    measurement.unit = std::move(unit);
    measurement.verdict = std::isfinite(value) && value >= lower && value <= upper
        ? RunVerdict::Ok : RunVerdict::Fail;
    if (measurement.verdict == RunVerdict::Fail) {
        measurement.message = "Значение вне допуска";
    }
    return measurement;
}

ProcedureResult telemetryHealth(const ScenarioNode& node, ProcedureContext& context)
{
    const auto response = values(context.equipment.invoke(
        "orbita.parameter_source", "health", {}));
    if (response.find("status") == response.end()
        || response.at("status") != "ready") {
        return {RunVerdict::Error,
                "Поток Орбита не готов: "
                    + (response.count("diagnostic") ? response.at("diagnostic")
                                                     : std::string("нет актуального кадра")),
                {}};
    }

    const double frames = requiredNumber(response, "frames_processed");
    const double phraseErrors = requiredNumber(response, "phrase_error_percent");
    const double groupErrors = requiredNumber(response, "group_error_percent");
    const double channels = requiredNumber(response, "channel_count");
    ProcedureResult result{RunVerdict::Ok, "Поток Орбита принимается и дешифруется", {}};
    result.measurements.push_back(checked(
        "orbita.frames", "Принятые группы", frames, 1.0,
        9.22e18, "шт."));
    result.measurements.push_back(checked(
        "orbita.phrase_errors", "Ошибки фраз", phraseErrors, 0.0,
        argumentNumber(node, "max_phrase_error_percent", 0.0), "%"));
    result.measurements.push_back(checked(
        "orbita.group_errors", "Ошибки групп", groupErrors, 0.0,
        argumentNumber(node, "max_group_error_percent", 0.0), "%"));
    result.measurements.push_back(checked(
        "orbita.channels", "Настроенные параметры", channels, 1.0,
        1000000.0, "шт."));
    for (const auto& measurement : result.measurements) {
        result.verdict = combineVerdicts(result.verdict, measurement.verdict);
    }
    if (flag(node, "diagnostic_only")) {
        result.verdict = RunVerdict::Incomplete;
        result.message = "Диагностика потока выполнена; блок по ТУ не оценивался";
    }
    return result;
}

} // namespace

void registerTelemetryProcedures(ScenarioEngine& engine)
{
    engine.registerProcedure("telemetry.health", telemetryHealth);
}

} // namespace orbita::stand
