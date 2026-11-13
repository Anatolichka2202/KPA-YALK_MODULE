#include "orbita_stand/parameter_procedures.h"
#include "orbita_stand/parameter_repository.h"
#include "orbita_stand/scenario.h"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
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
            && key != "upper" && key != "unit" && key != "timeout_ms"
            && key != "conversion" && key != "scale" && key != "offset" && key != "output_unit")
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
    const auto conversion = node.arguments.find("conversion");
    const auto mode = conversion == node.arguments.end() ? std::string("identity") : conversion->second;
    if (mode != "identity" && mode != "linear") errors.push_back("Неизвестное преобразование параметра");
    if (mode == "linear") {
        for (const auto* key : {"scale", "offset"}) {
            try {
                const auto& text = node.arguments.at(key);
                std::size_t parsed = 0;
                const double value = std::stod(text, &parsed);
                if (parsed != text.size() || !std::isfinite(value)) throw std::invalid_argument("coefficient");
            } catch (const std::exception&) {
                errors.push_back(std::string("Неверный коэффициент преобразования: ") + key);
            }
        }
        const auto output = node.arguments.find("output_unit");
        if (output == node.arguments.end() || output->second.empty())
            errors.push_back("Не указана выходная единица преобразования");
    } else if (node.arguments.count("scale") || node.arguments.count("offset") || node.arguments.count("output_unit")) {
        errors.push_back("Коэффициенты заданы без линейного преобразования");
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
    const auto conversion = node.arguments.find("conversion");
    const bool linear = conversion != node.arguments.end() && conversion->second == "linear";
    measurement.measured = linear
        ? sample->physicalValue * std::stod(node.arguments.at("scale")) + std::stod(node.arguments.at("offset"))
        : sample->physicalValue;
    if (!std::isfinite(measurement.measured))
        return {RunVerdict::Error, "Результат преобразования не является конечным числом", {}};
    measurement.unit = linear ? node.arguments.at("output_unit") : sample->unit;
    measurement.verdict = measurement.measured >= measurement.lowerLimit
        && measurement.measured <= measurement.upperLimit ? RunVerdict::Ok : RunVerdict::Fail;
    measurement.message = measurement.verdict == RunVerdict::Ok ? "В пределах допуска" : "Значение вне допуска";
    // Логическая роль оборудования не известна: не подменяем её именем
    // источника. Подтверждённые происхождение и качество сохраняем отдельно.
    measurement.attributes["source_id"] = source;
    measurement.attributes["sample_quality"] = "GOOD";
    std::ostringstream input;
    input << std::setprecision(17) << sample->physicalValue;
    measurement.attributes["input_value"] = input.str();
    measurement.attributes["input_unit"] = sample->unit;
    measurement.attributes["conversion"] = linear ? "linear" : "identity";
    if (linear) {
        measurement.attributes["scale"] = node.arguments.at("scale");
        measurement.attributes["offset"] = node.arguments.at("offset");
    }
    measurement.attributes["sample_sequence"] = std::to_string(sample->sequence);
    measurement.attributes["sample_time_ms"] = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        sample->timestamp.time_since_epoch()).count());
    return {measurement.verdict, measurement.message, {measurement}};
}

std::vector<std::string> validateWait(const ScenarioNode& node)
{
    auto copy = node;
    copy.arguments.erase("stable_ms");
    copy.arguments.erase("max_gap_ms");
    auto errors = validateMeasurement(copy);
    try {
        const auto& text = node.arguments.at("stable_ms");
        std::size_t used = 0;
        const auto duration = std::stoul(text, &used);
        const auto timeout = std::stoul(node.arguments.at("timeout_ms"));
        if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos
            || used != text.size() || duration > timeout)
            throw std::invalid_argument("stable");
    } catch (const std::exception&) {
        errors.push_back("Время устойчивости должно быть от 0 до времени ожидания");
    }
    try {
        const auto& text = node.arguments.at("max_gap_ms");
        std::size_t used = 0;
        const auto gap = std::stoul(text, &used);
        if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos
            || used != text.size() || gap < 1 || gap > 5000) throw std::invalid_argument("gap");
    } catch (const std::exception&) {
        errors.push_back("Допустимый разрыв между отсчётами должен быть от 1 до 5000 мс");
    }
    return errors;
}

ProcedureResult waitForParameter(const ScenarioNode& node, ProcedureContext& context, ParameterRepository& repository)
{
    const auto& source = node.arguments.at("source");
    const auto& key = node.arguments.at("parameter");
    const auto startedWall = std::chrono::system_clock::now();
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(std::stoul(node.arguments.at("timeout_ms")));
    const auto stableDuration = std::chrono::milliseconds(std::stoul(node.arguments.at("stable_ms")));
    const auto maxGap = std::chrono::milliseconds(std::stoul(node.arguments.at("max_gap_ms")));
    const auto lower = std::stod(node.arguments.at("lower"));
    const auto upper = std::stod(node.arguments.at("upper"));
    const bool linear = node.arguments.count("conversion") && node.arguments.at("conversion") == "linear";
    const auto scale = linear ? std::stod(node.arguments.at("scale")) : 1.0;
    const auto offset = linear ? std::stod(node.arguments.at("offset")) : 0.0;
    std::optional<std::chrono::steady_clock::time_point> stableSince, lastDistinctAt;
    std::optional<std::chrono::system_clock::time_point> stableSampleSince;
    std::optional<ParameterSample> lastDistinct;
    std::optional<MeasurementResult> candidate;
    std::uint64_t cursor = 0;
    try { cursor = repository.observationsAfter(source, key, std::numeric_limits<std::uint64_t>::max()).lastRevision; }
    catch (const std::exception& error) { return {RunVerdict::Error, std::string("Источник недоступен: ") + error.what(), {}}; }
    do {
        if (context.stopRequested.load()) return {RunVerdict::Aborted, "Ожидание остановлено", {}};
        ParameterObservationBatch batch;
        try { batch = repository.observationsAfter(source, key, cursor); }
        catch (const std::exception& error) { return {RunVerdict::Error, std::string("Источник недоступен: ") + error.what(), {}}; }
        cursor = batch.lastRevision;
        // Потеря даже одного промежуточного отсчёта запрещает считать
        // предыдущее окно непрерывно устойчивым.
        if (batch.overflow) {
            stableSince.reset(); stableSampleSince.reset(); lastDistinct.reset(); lastDistinctAt.reset(); candidate.reset();
        }
        for (const auto& observation : batch.observations) {
            if (observation.received > deadline) continue;
            const auto& sample = observation.sample;
            const auto wallNow = std::chrono::system_clock::now();
            if (sample.quality != SampleQuality::Good || sample.timestamp < startedWall || sample.timestamp > wallNow
                || wallNow - sample.timestamp > maxGap || sample.unit != node.arguments.at("unit")
                || !std::isfinite(sample.physicalValue)) {
                stableSince.reset(); stableSampleSince.reset(); lastDistinct.reset(); lastDistinctAt.reset(); candidate.reset(); continue;
            }
            const double value = sample.physicalValue * scale + offset;
            if (!std::isfinite(value) || value < lower || value > upper) {
                stableSince.reset(); stableSampleSince.reset(); lastDistinct.reset(); lastDistinctAt.reset(); candidate.reset(); continue;
            }
            const bool distinct = !lastDistinct || sample.sequence != lastDistinct->sequence
                || sample.timestamp != lastDistinct->timestamp;
            if (!distinct) {
                // Один идентификатор отсчёта не может обозначать два значения.
                if (sample.physicalValue != lastDistinct->physicalValue || sample.rawValue != lastDistinct->rawValue) {
                    stableSince.reset(); stableSampleSince.reset(); lastDistinct.reset(); lastDistinctAt.reset(); candidate.reset();
                }
                continue;
            }
            if (lastDistinctAt && (observation.received - *lastDistinctAt > maxGap
                    || sample.timestamp <= lastDistinct->timestamp
                    || sample.timestamp - lastDistinct->timestamp > maxGap)) {
                stableSince.reset(); stableSampleSince.reset(); candidate.reset();
            }
            if (!stableSince) { stableSince = observation.received; stableSampleSince = sample.timestamp; }
            lastDistinct = sample;
            lastDistinctAt = observation.received;
            if (observation.received - *stableSince >= stableDuration
                && sample.timestamp - *stableSampleSince >= stableDuration) {
                MeasurementResult result;
                result.parameterKey = key;
                result.title = node.title.empty() ? key : node.title;
                result.measured = value;
                result.lowerLimit = lower; result.upperLimit = upper;
                result.unit = linear ? node.arguments.at("output_unit") : sample.unit;
                result.verdict = RunVerdict::Ok;
                result.message = "Наблюдаемые значения устойчивы в диапазоне";
                result.attributes = {{"source_id", source}, {"sample_quality", "GOOD"},
                    {"sample_sequence", std::to_string(sample.sequence)}, {"stable_ms", node.arguments.at("stable_ms")},
                    {"max_gap_ms", node.arguments.at("max_gap_ms")},
                    {"sample_time_ms", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                        sample.timestamp.time_since_epoch()).count())}};
                std::ostringstream input;
                input << std::setprecision(17) << sample.physicalValue;
                result.attributes["input_value"] = input.str();
                result.attributes["input_unit"] = sample.unit;
                result.attributes["conversion"] = linear ? "linear" : "identity";
                if (linear) {
                    result.attributes["scale"] = node.arguments.at("scale");
                    result.attributes["offset"] = node.arguments.at("offset");
                }
                candidate = std::move(result);
            }
        }
        const auto now = std::chrono::steady_clock::now();
        const auto wallNow = std::chrono::system_clock::now();
        if (candidate && lastDistinctAt && lastDistinct && now <= deadline
            && now - *lastDistinctAt <= maxGap && wallNow >= lastDistinct->timestamp
            && wallNow - lastDistinct->timestamp <= maxGap
            && !context.stopRequested.load()) {
            // Прежде чем принять результат, убедимся под блокировкой кэша,
            // что за время обработки не поступило новых наблюдений.
            try {
                const auto current = repository.observationsAfter(source, key,
                    std::numeric_limits<std::uint64_t>::max());
                if (current.lastRevision == cursor)
                    return {RunVerdict::Ok, candidate->message, {*candidate}};
            } catch (const std::exception& error) {
                return {RunVerdict::Error, std::string("Источник недоступен: ") + error.what(), {}};
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (std::chrono::steady_clock::now() < deadline);
    if (context.stopRequested.load()) return {RunVerdict::Aborted, "Ожидание остановлено", {}};
    return {RunVerdict::Error, "Истекло время ожидания устойчивого параметра", {}};
}
}
void registerParameterProcedures(ScenarioEngine& engine, ParameterRepository& parameters)
{
    engine.registerProcedure("parameter.measure", [&parameters](const ScenarioNode& node, ProcedureContext& context) {
        return measure(node, context, parameters);
    }, validateMeasurement);
    engine.registerProcedure("parameter.wait", [&parameters](const ScenarioNode& node, ProcedureContext& context) {
        return waitForParameter(node, context, parameters);
    }, validateWait);
}
}
