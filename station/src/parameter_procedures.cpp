#include "orbita_stand/parameter_procedures.h"
#include "orbita_stand/parameter_repository.h"
#include "orbita_stand/scenario.h"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <thread>

namespace orbita::stand {
namespace {
std::vector<std::string> validateMeasurement(const ScenarioNode& node)
{
    std::vector<std::string> errors;
    for (const auto& [key, value] : node.arguments) {
        (void)value;
        if (key != "source" && key != "parameter" && key != "lower"
            && key != "upper" && key != "unit" && key != "timeout_ms")
            errors.push_back("Неизвестный параметр измерения: " + key);
    }
    for (const auto* key : {"source", "parameter", "unit"}) {
        auto item = node.arguments.find(key);
        if (item == node.arguments.end() || item->second.empty())
            errors.push_back(std::string("Не задан обязательный параметр измерения: ") + key);
    }
    double bounds[2]{};
    bool validBounds = true;
    int index = 0;
    for (const auto* key : {"lower", "upper"}) {
        try {
            const auto& text = node.arguments.at(key);
            std::size_t parsed = 0;
            bounds[index] = std::stod(text, &parsed);
            if (parsed != text.size() || !std::isfinite(bounds[index])) throw std::invalid_argument("limit");
        } catch (const std::exception&) {
            errors.push_back(std::string("Не задан числовой предел: ") + key);
            validBounds = false;
        }
        ++index;
    }
    try {
        const auto& text = node.arguments.at("timeout_ms");
        std::size_t parsed = 0;
        const auto timeout = std::stoul(text, &parsed);
        if (parsed != text.size() || timeout < 500 || timeout > 600000)
            throw std::invalid_argument("timeout");
    } catch (const std::exception&) {
        errors.push_back("Время ожидания должно быть от 500 до 600000 мс");
    }
    if (validBounds && bounds[0] > bounds[1]) errors.push_back("Нижний предел больше верхнего");
    return errors;
}

ProcedureResult measure(const ScenarioNode& node, ProcedureContext& context, ParameterRepository& repository)
{
    const auto& source = node.arguments.at("source");
    const auto& parameter = node.arguments.at("parameter");
    // Зафиксированный до шага снимок не доказывает состояние после предыдущего
    // воздействия: ждём другого отсчёта, даже если старый ещё не устарел.
    std::optional<ParameterSample> baseline, sample;
    const auto startedAt = std::chrono::system_clock::now();
    try { baseline = repository.latest(source, parameter, std::chrono::seconds(5)); }
    catch (const std::exception& error) {
        return {RunVerdict::Error, std::string("Источник недоступен: ") + error.what(), {}};
    }
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(std::stoul(node.arguments.at("timeout_ms")));
    bool fresh = false;
    do {
        if (context.stopRequested.load()) return {RunVerdict::Aborted, "Измерение остановлено", {}};
        try { sample = repository.latest(source, parameter, std::chrono::seconds(5)); }
        catch (const std::exception& error) {
            return {RunVerdict::Error, std::string("Источник недоступен: ") + error.what(), {}};
        }
        fresh = sample && sample->timestamp >= startedAt && (!baseline
            || sample->sequence != baseline->sequence || sample->timestamp != baseline->timestamp);
        if (fresh && sample->quality == SampleQuality::Good) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (std::chrono::steady_clock::now() < deadline);
    if (!fresh || !sample || sample->quality != SampleQuality::Good || !std::isfinite(sample->physicalValue))
        return {RunVerdict::Error, "Нет нового достоверного значения параметра", {}};
    if (node.arguments.at("unit") != sample->unit)
        return {RunVerdict::Error, "Единица измерения источника не соответствует настройке", {}};
    MeasurementResult measurement;
    measurement.parameterKey = parameter;
    measurement.title = node.title.empty() ? parameter : node.title;
    measurement.lowerLimit = std::stod(node.arguments.at("lower"));
    measurement.upperLimit = std::stod(node.arguments.at("upper"));
    measurement.measured = sample->physicalValue;
    measurement.unit = sample->unit;
    measurement.verdict = sample->physicalValue >= measurement.lowerLimit
        && sample->physicalValue <= measurement.upperLimit ? RunVerdict::Ok : RunVerdict::Fail;
    measurement.message = measurement.verdict == RunVerdict::Ok ? "В пределах допуска" : "Значение вне допуска";
    // Логическая роль оборудования не известна: не подменяем её именем
    // источника. Подтверждённые происхождение и качество сохраняем отдельно.
    measurement.attributes["source_id"] = source;
    measurement.attributes["sample_quality"] = "GOOD";
    measurement.attributes["sample_sequence"] = std::to_string(sample->sequence);
    measurement.attributes["sample_time_ms"] = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        sample->timestamp.time_since_epoch()).count());
    return {measurement.verdict, measurement.message, {measurement}};
}
}
void registerParameterProcedures(ScenarioEngine& engine, ParameterRepository& parameters)
{
    engine.registerProcedure("parameter.measure", [&parameters](const ScenarioNode& node, ProcedureContext& context) {
        return measure(node, context, parameters);
    }, validateMeasurement);
}
}
