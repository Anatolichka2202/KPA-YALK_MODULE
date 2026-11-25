#include "orbita_stand/scenario.h"
#include "orbita_stand/resource_lease.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <thread>

namespace orbita::stand {
namespace {

std::string newRunId()
{
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    std::random_device random;
    std::ostringstream stream;
    stream << millis << '-' << std::hex << std::setw(8) << std::setfill('0') << random();
    return stream.str();
}

bool isTerminal(RunVerdict verdict)
{
    return verdict == RunVerdict::Error || verdict == RunVerdict::Aborted;
}

bool shouldStop(RunVerdict verdict, bool allowPartial)
{
    return isTerminal(verdict)
        || (!allowPartial && verdict == RunVerdict::Incomplete);
}

bool retryLimit(const ScenarioNode& node, unsigned& value)
{
    value = node.policy.technicalRetries;
    return value <= 3;
}

void collectRequiredResources(
    const std::vector<ScenarioNode>& nodes,
    std::set<std::string>& resources)
{
    for (const auto& node : nodes) {
        for (const auto& requirement : node.requiredResources) {
            // validate() reports an empty id. Do not create a meaningless
            // lease before that validation is known to have passed.
            if (!requirement.resource.empty()) resources.insert(requirement.resource);
        }
        collectRequiredResources(node.children, resources);
    }
}

struct ResourceRoute {
    const ResourceRequirement* requirement = nullptr;
    bool ambiguous = false;
};

ResourceRoute resourceRoute(const ScenarioNode& node, const std::string& capability)
{
    ResourceRoute result;
    for (const auto& requirement : node.requiredResources) {
        if (requirement.capability != capability) continue;
        if (result.requirement) {
            result.ambiguous = true;
            return result;
        }
        result.requirement = &requirement;
    }
    return result;
}

// Temporary compatibility view for procedures that still call invoke(capability).
// The scenario already owns the routing decision: if the current step names one
// resource for that capability, the old call is scoped to that resource. Once
// KTMA procedures are migrated to explicit resource calls this adapter can go.
class StepEquipment final : public ICapabilityProvider {
public:
    StepEquipment(ICapabilityProvider& upstream, const ScenarioNode& node)
        : upstream_(upstream), node_(node)
    {
    }

    bool hasCapability(const std::string& capability) const override
    {
        const auto route = resourceRoute(node_, capability);
        if (route.ambiguous) return false;
        return route.requirement
            ? upstream_.resourceHasCapability(route.requirement->resource, capability)
            : upstream_.hasCapability(capability);
    }

    std::string invoke(
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments) override
    {
        const auto route = resourceRoute(node_, capability);
        if (route.ambiguous) {
            throw std::runtime_error(
                "Неоднозначный вызов capability " + capability + " в шаге " + node_.id
                + ": процедура должна выбрать ресурс явно");
        }
        return route.requirement
            ? upstream_.invokeResource(
                route.requirement->resource, capability, operation, arguments)
            : upstream_.invoke(capability, operation, arguments);
    }

    bool resourceHasCapability(
        const std::string& resource,
        const std::string& capability) const override
    {
        return upstream_.resourceHasCapability(resource, capability);
    }

    std::string invokeResource(
        const std::string& resource,
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments) override
    {
        return upstream_.invokeResource(resource, capability, operation, arguments);
    }

    void safeStopAll() noexcept override
    {
        upstream_.safeStopAll();
    }

private:
    ICapabilityProvider& upstream_;
    const ScenarioNode& node_;
};

std::string missingDependencies(const ScenarioNode& node, ICapabilityProvider& equipment)
{
    std::vector<std::string> missingCapabilities;
    std::vector<const ResourceRequirement*> missingResources;

    for (const auto& capability : node.requiredCapabilities) {
        // A migrated resource requirement is authoritative. Keeping the same
        // capability in legacy `requires:` must not force a second global bind.
        if (resourceRoute(node, capability).requirement) continue;
        if (!equipment.hasCapability(capability)) missingCapabilities.push_back(capability);
    }
    for (const auto& requirement : node.requiredResources) {
        if (!equipment.resourceHasCapability(requirement.resource, requirement.capability)) {
            missingResources.push_back(&requirement);
        }
    }

    if (missingCapabilities.empty() && missingResources.empty()) return {};

    std::ostringstream message;
    if (!missingCapabilities.empty()) {
        message << "Недоступны возможности: ";
        for (std::size_t index = 0; index < missingCapabilities.size(); ++index) {
            if (index) message << ", ";
            message << missingCapabilities[index];
        }
    }
    if (!missingResources.empty()) {
        if (!missingCapabilities.empty()) message << "; ";
        message << "Недоступны ресурсы: ";
        for (std::size_t index = 0; index < missingResources.size(); ++index) {
            if (index) message << ", ";
            message << missingResources[index]->resource << ':'
                    << missingResources[index]->capability;
        }
    }
    return message.str();
}

void validateNode(
    const ScenarioNode& node,
    const std::map<std::string, ProcedureFunction>& procedures,
    const std::map<std::string, ProcedureValidator>& validators,
    std::set<std::string>& ids,
    std::vector<std::string>& errors)
{
    if (node.id.empty()) {
        errors.emplace_back("У шага отсутствует id");
    } else if (!ids.insert(node.id).second) {
        errors.emplace_back("Повторяющийся id шага: " + node.id);
    }
    if (node.title.empty()) errors.emplace_back("У шага " + node.id + " отсутствует название");
    if (!node.children.empty() && !node.procedure.empty()) {
        errors.emplace_back("Шаг " + node.id + " не может одновременно быть процедурой и группой");
    }

    if (node.repeatCount < 1 || node.repeatCount > 100)
        errors.emplace_back("Число проходов группы должно быть от 1 до 100: " + node.id);
    if (node.repeatCount != 1 && node.children.empty())
        errors.emplace_back("Повтор можно задать только для группы: " + node.id);

    if (!node.repeatRows.empty()) {
        if (node.children.empty() || node.repeatCount < 2 || node.repeatRows.size() != node.repeatCount)
            errors.emplace_back("Число строк таблицы должно совпадать с числом проходов группы: " + node.id);
        for (const auto& row : node.repeatRows) {
            std::set<std::pair<std::string, std::string>> targets;
            for (const auto& value : row) {
                if (value.stepId.empty() || value.argument.empty() || !targets.emplace(value.stepId, value.argument).second) {
                    errors.emplace_back("Повторяющийся или пустой параметр таблицы группы: " + node.id);
                    continue;
                }
                const ScenarioNode* target = nullptr;
                bool nestedRepeat = false;
                std::function<void(const ScenarioNode&, bool)> find = [&](const ScenarioNode& child, bool nested) {
                    const bool inside = nested || child.repeatCount > 1;
                    if (child.id == value.stepId) { target = &child; nestedRepeat = inside; }
                    for (const auto& descendant : child.children) find(descendant, inside);
                };
                for (const auto& child : node.children) find(child, false);
                if (!target || !target->children.empty() || nestedRepeat || !target->arguments.count(value.argument))
                    errors.emplace_back("Таблица ссылается на отсутствующий параметр или вложенный повтор: "
                        + value.stepId + "." + value.argument);
                else if ((target->procedure == "parameter.measure" || target->procedure == "parameter.wait")
                    && (value.argument == "source" || value.argument == "parameter" || value.argument == "unit"
                        || value.argument == "conversion" || value.argument == "scale" || value.argument == "offset"
                        || value.argument == "output_unit"))
                    errors.emplace_back("Источник и преобразование параметра настраиваются в самом действии: " + value.stepId);
            }
        }
    }

    std::set<std::string> resources;
    for (const auto& requirement : node.requiredResources) {
        if (requirement.resource.empty() || requirement.capability.empty()) {
            errors.emplace_back(
                "У шага " + node.id + " некорректное требование ресурса: нужны resource и capability");
            continue;
        }
        const std::string key = requirement.resource + '\n' + requirement.capability;
        if (!resources.insert(key).second) {
            errors.emplace_back(
                "У шага " + node.id + " повторяется требование ресурса "
                + requirement.resource + ':' + requirement.capability);
        }
    }

    unsigned retries = 0;
    if (!retryLimit(node, retries)) {
        errors.emplace_back(
            "У шага " + node.id + " число технических повторов должно быть в диапазоне 0..3");
    }

    if (node.children.empty()) {
        if (node.procedure.empty()) {
            errors.emplace_back("У конечного шага " + node.id + " отсутствует процедура");
        } else if (!procedures.count(node.procedure)) {
            errors.emplace_back(
                "Не зарегистрирована процедура " + node.procedure + " для шага " + node.id);
        }
        const auto validator = validators.find(node.procedure);
        if (validator != validators.end()) {
            for (const auto& error : validator->second(node))
                errors.emplace_back("Шаг " + node.id + ": " + error);
        }
        if (node.tuRequirement.empty()) {
            errors.emplace_back("У конечного шага " + node.id + " отсутствует ссылка на пункт ТУ");
        }
    }

    for (const auto& child : node.children) {
        validateNode(child, procedures, validators, ids, errors);
    }
}

// Разворачиваем повторы в обычное дерево до запуска: те же механизмы
// событий и контрольных точек сохраняют отдельный результат каждого прохода.
std::vector<ScenarioNode> expandRepeats(const std::vector<ScenarioNode>& roots)
{
    std::set<std::string> occupied;
    std::function<void(const ScenarioNode&)> collect = [&](const ScenarioNode& node) {
        occupied.insert(node.id);
        for (const auto& child : node.children) collect(child);
    };
    for (const auto& root : roots) collect(root);
    std::uint64_t sequence = 0;
    std::size_t total = 0;
    const auto nextId = [&] {
        std::string id;
        do { id = "__iteration_" + std::to_string(++sequence); } while (!occupied.insert(id).second);
        return id;
    };
    std::function<ScenarioNode(const ScenarioNode&, bool)> expand;
    expand = [&](const ScenarioNode& source, bool cloned) {
        if (++total > 10000) throw std::invalid_argument("Слишком много шагов после раскрытия повторов");
        ScenarioNode out = source;
        if (cloned) { out.definitionId = source.id; out.id = nextId(); }
        out.repeatCount = 1;
        out.repeatRows.clear();
        out.children.clear();
        if (source.repeatCount > 1) {
            for (unsigned pass = 1; pass <= source.repeatCount; ++pass) {
                if (++total > 10000) throw std::invalid_argument("Слишком много шагов после раскрытия повторов");
                ScenarioNode iteration;
                iteration.id = nextId();
                iteration.definitionId = source.id;
                iteration.title = "Проход " + std::to_string(pass) + " из " + std::to_string(source.repeatCount);
                auto children = source.children;
                if (!source.repeatRows.empty()) {
                    for (const auto& overrideValue : source.repeatRows.at(pass - 1)) {
                        std::function<void(ScenarioNode&)> apply = [&](ScenarioNode& child) {
                            if (child.id == overrideValue.stepId) {
                                child.arguments.at(overrideValue.argument) = overrideValue.value;
                                child.appliedOverrides[overrideValue.argument] = overrideValue.value;
                            }
                            for (auto& nested : child.children) apply(nested);
                        };
                        for (auto& child : children) apply(child);
                    }
                }
                for (const auto& child : children) iteration.children.push_back(expand(child, true));
                out.children.push_back(std::move(iteration));
            }
        } else {
            for (const auto& child : source.children) out.children.push_back(expand(child, cloned));
        }
        return out;
    };
    std::vector<ScenarioNode> result;
    for (const auto& root : roots) result.push_back(expand(root, false));
    return result;
}

} // namespace

const char* toString(RunVerdict verdict) noexcept
{
    switch (verdict) {
    case RunVerdict::NotRun: return "NOT_RUN";
    case RunVerdict::Ok: return "OK";
    case RunVerdict::Fail: return "FAIL";
    case RunVerdict::Incomplete: return "INCOMPLETE";
    case RunVerdict::Error: return "ERROR";
    case RunVerdict::Aborted: return "ABORTED";
    }
    return "ERROR";
}

RunVerdict combineVerdicts(RunVerdict current, RunVerdict next) noexcept
{
    const auto rank = [](RunVerdict verdict) {
        switch (verdict) {
        case RunVerdict::NotRun: return 0;
        case RunVerdict::Ok: return 1;
        case RunVerdict::Incomplete: return 2;
        case RunVerdict::Fail: return 3;
        case RunVerdict::Error: return 4;
        case RunVerdict::Aborted: return 5;
        }
        return 4;
    };
    return rank(next) > rank(current) ? next : current;
}

void ScenarioEngine::registerProcedure(std::string id, ProcedureFunction procedure,
                                       ProcedureValidator validator)
{
    if (id.empty() || !procedure) {
        throw std::invalid_argument("Procedure id and callback are required");
    }
    // Перерегистрация заменяет и проверку, чтобы для новой процедуры не
    // остались ограничения предыдущей реализации с тем же идентификатором.
    if (validator) validators_[id] = std::move(validator);
    else validators_.erase(id);
    procedures_[std::move(id)] = std::move(procedure);
}

std::vector<std::string> ScenarioEngine::registeredProcedures() const
{
    std::vector<std::string> result;
    result.reserve(procedures_.size());
    for (const auto& procedure : procedures_) result.push_back(procedure.first);
    return result;
}

std::vector<std::string> ScenarioEngine::validate(const ScenarioDefinition& scenario) const
{
    std::vector<std::string> errors;
    if (scenario.id.empty()) errors.emplace_back("У сценария отсутствует id");
    if (scenario.title.empty()) errors.emplace_back("У сценария отсутствует название");
    if (scenario.version.empty()) errors.emplace_back("У сценария отсутствует версия");
    if (scenario.catalogVersion.empty()) errors.emplace_back("У сценария отсутствует версия каталога");
    if (scenario.steps.empty()) errors.emplace_back("Сценарий не содержит шагов");

    std::set<std::string> ids;
    for (const auto& node : scenario.steps) {
        validateNode(node, procedures_, validators_, ids, errors);
    }
    if (errors.empty()) {
        try {
            const auto expanded = expandRepeats(scenario.steps);
            std::set<std::string> expandedIds;
            for (const auto& node : expanded) validateNode(node, procedures_, validators_, expandedIds, errors);
        } catch (const std::exception& error) { errors.emplace_back(error.what()); }
    }
    return errors;
}

StepRunResult ScenarioEngine::runNode(
    const ScenarioNode& node,
    ProcedureContext& context,
    bool allowPartial)
{
    StepRunResult result;
    result.nodeId = node.id;
    result.definitionId = node.definitionId;
    result.appliedOverrides = node.appliedOverrides;
    result.title = node.title;
    result.tuRequirement = node.tuRequirement;

    if (context.stopRequested.load()) {
        result.verdict = RunVerdict::Aborted;
        result.message = "Остановлено оператором";
        return result;
    }

    context.eventSink({
        std::chrono::system_clock::now(), node.id, "START", node.title,
        RunVerdict::NotRun});

    if (!node.children.empty()) {
        result.verdict = RunVerdict::Ok;
        for (const auto& child : node.children) {
            auto childResult = runNode(child, context, allowPartial);
            const auto childVerdict = childResult.verdict;
            result.verdict = combineVerdicts(result.verdict, childVerdict);
            result.children.push_back(std::move(childResult));
            if (context.stopRequested.load()) {
                result.verdict = combineVerdicts(result.verdict, RunVerdict::Aborted);
                break;
            }
            if (shouldStop(childVerdict, allowPartial)) break;
        }
        result.message = result.verdict == RunVerdict::Ok
            ? "Все вложенные проверки выполнены"
            : "Группа содержит проверки без результата или с отклонениями";
    } else {
        const auto missing = missingDependencies(node, context.equipment);
        if (!missing.empty()) {
            result.verdict = RunVerdict::Incomplete;
            result.message = missing;
        } else {
            const auto procedure = procedures_.find(node.procedure);
            if (procedure == procedures_.end()) {
                result.verdict = RunVerdict::Incomplete;
                result.message = "Процедура не зарегистрирована: " + node.procedure;
            } else {
                StepEquipment scopedEquipment(context.equipment, node);
                const auto publishProcedureEvent = [&context, &node](const RunEvent& event) {
                    auto occurrenceEvent = event;
                    if (!node.definitionId.empty() && occurrenceEvent.nodeId == node.definitionId)
                        occurrenceEvent.nodeId = node.id;
                    context.eventSink(occurrenceEvent);
                };
                ProcedureContext scopedContext{
                    scopedEquipment,
                    context.stopRequested,
                    publishProcedureEvent,
                    context.runId,
                    std::move(context.state),
                };
                auto procedureNode = node;
                if (!node.definitionId.empty()) procedureNode.id = node.definitionId;

                unsigned retries = 0;
                retryLimit(node, retries); // validate() already checked it.
                for (unsigned attempt = 0; attempt <= retries; ++attempt) {
                    try {
                        auto procedureResult = procedure->second(procedureNode, scopedContext);
                        result.verdict = procedureResult.verdict;
                        result.message = std::move(procedureResult.message);
                        result.measurements = std::move(procedureResult.measurements);
                    } catch (const std::exception& error) {
                        result.verdict = RunVerdict::Error;
                        result.message = error.what();
                    } catch (...) {
                        result.verdict = RunVerdict::Error;
                        result.message = "Неизвестная ошибка процедуры";
                    }

                    if (context.stopRequested.load()
                        || result.verdict != RunVerdict::Error
                        || attempt == retries) {
                        break;
                    }

                    context.equipment.safeStopAll();
                    context.eventSink({
                        std::chrono::system_clock::now(), node.id, "RETRY",
                        "Техническая ошибка; безопасный сброс выполнен, повтор "
                            + std::to_string(attempt + 1) + " из "
                            + std::to_string(retries),
                        RunVerdict::Error,
                        {{"attempt", std::to_string(attempt + 2)},
                         {"max_retries", std::to_string(retries)},
                         {"error", result.message}}});
                }

                context.state = std::move(scopedContext.state);
            }
        }
    }

    context.eventSink({
        std::chrono::system_clock::now(), node.id, "FINISH", result.message,
        result.verdict});
    if (node.children.empty() && context.stepCompleted) context.stepCompleted(result);
    return result;
}

ScenarioRunResult makeInitialRun(const ScenarioDefinition& scenario,
                                 std::string profileVersion, std::string objectSerial)
{
    ScenarioRunResult run;
    run.runId = newRunId();
    run.scenarioId = scenario.id;
    run.scenarioTitle = scenario.title;
    run.scenarioVersion = scenario.version;
    run.catalogVersion = scenario.catalogVersion;
    run.profileVersion = std::move(profileVersion);
    run.objectSerial = std::move(objectSerial);
    run.startedAt = std::chrono::system_clock::now();
    run.verdict = RunVerdict::Incomplete;
    return run;
}

void registerTimingProcedures(ScenarioEngine& engine)
{
    const auto validate = [](const ScenarioNode& node) {
        std::vector<std::string> errors;
        for (const auto& [key, value] : node.arguments) {
            (void)value;
            if (key != "duration_ms") errors.push_back("Неизвестный параметр выдержки: " + key);
        }
        const auto it = node.arguments.find("duration_ms");
        if (it == node.arguments.end() || it->second.empty()
            || !std::all_of(it->second.begin(), it->second.end(),
                [](unsigned char c) { return c >= '0' && c <= '9'; })) {
            errors.push_back("Длительность выдержки должна быть целым числом миллисекунд");
        } else {
            try {
                const auto duration = std::stoull(it->second);
                if (duration < 1 || duration > 3600000) throw std::out_of_range("duration");
            } catch (const std::exception&) {
                errors.push_back("Длительность выдержки должна быть от 1 до 3600000 мс");
            }
        }
        return errors;
    };
    engine.registerProcedure("station.delay", [](const ScenarioNode& node, ProcedureContext& context) {
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(std::stoll(node.arguments.at("duration_ms")));
        while (std::chrono::steady_clock::now() < deadline) {
            if (context.stopRequested.load()) return ProcedureResult{RunVerdict::Aborted, "Выдержка остановлена", {}};
            std::this_thread::sleep_for(std::min(std::chrono::milliseconds(20),
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())));
        }
        if (context.stopRequested.load()) return ProcedureResult{RunVerdict::Aborted, "Выдержка остановлена", {}};
        return ProcedureResult{RunVerdict::Ok, "Выдержка завершена", {}};
    }, validate);
}

void registerManualProcedures(ScenarioEngine& engine)
{
    const auto validate = [](const ScenarioNode& node) {
        std::vector<std::string> errors;
        for (const auto& [key, value] : node.arguments) {
            (void)value;
            if (key != "instruction") errors.push_back("Неизвестный параметр ручной инструкции: " + key);
        }
        const auto instruction = node.arguments.find("instruction");
        if (instruction == node.arguments.end() || instruction->second.find_first_not_of(" \t\r\n") == std::string::npos
            || instruction->second.find_first_of("\r\n") != std::string::npos)
            errors.push_back("Укажите непустую однострочную инструкцию оператору");
        if (!node.requiredCapabilities.count("operator.manual_input"))
            errors.push_back("Для инструкции требуется подтверждение оператора");
        return errors;
    };
    engine.registerProcedure("operator.confirm", [](const ScenarioNode& node, ProcedureContext& context) {
        if (context.stopRequested.load()) return ProcedureResult{RunVerdict::Aborted, "Проверка остановлена", {}};
        const auto reply = context.equipment.invoke("operator.manual_input", "confirm_action", {
            {"title", node.title}, {"prompt", node.arguments.at("instruction")}});
        // Подтверждением считается только отдельное поле с точным значением.
        bool confirmed = false;
        std::istringstream lines(reply);
        std::string line;
        while (std::getline(lines, line)) if (line == "status=confirmed") confirmed = true;
        if (context.stopRequested.load()) return ProcedureResult{RunVerdict::Aborted, "Проверка остановлена", {}};
        if (!confirmed) return ProcedureResult{RunVerdict::Error, "Действие не подтверждено оператором", {}};
        return ProcedureResult{RunVerdict::Ok, "Действие подтверждено оператором", {}};
    }, validate);
    const auto validateMeasurement = [](const ScenarioNode& node) {
        std::vector<std::string> errors;
        for (const auto& [key, value] : node.arguments) {
            (void)value;
            if (key != "instruction" && key != "unit" && key != "lower" && key != "upper")
                errors.push_back("Неизвестный параметр ручного измерения: " + key);
        }
        for (const auto* key : {"instruction", "unit"}) {
            const auto found = node.arguments.find(key);
            if (found == node.arguments.end() || found->second.find_first_not_of(" \t\r\n") == std::string::npos
                || found->second.find_first_of("\r\n") != std::string::npos)
                errors.push_back(std::string("Не задано поле ручного измерения: ") + key);
        }
        double bounds[2]{};
        bool valid = true;
        int index = 0;
        for (const auto* key : {"lower", "upper"}) {
            try {
                const auto& text = node.arguments.at(key);
                std::size_t used = 0;
                bounds[index] = std::stod(text, &used);
                if (used != text.size() || !std::isfinite(bounds[index])) throw std::invalid_argument("limit");
            } catch (const std::exception&) {
                valid = false;
                errors.push_back(std::string("Неверный предел ручного измерения: ") + key);
            }
            ++index;
        }
        if (valid && bounds[0] > bounds[1]) errors.push_back("Нижний предел больше верхнего");
        if (!node.requiredCapabilities.count("operator.manual_input"))
            errors.push_back("Для ручного измерения требуется ввод оператора");
        return errors;
    };
    engine.registerProcedure("operator.measure", [](const ScenarioNode& node, ProcedureContext& context) {
        if (context.stopRequested.load()) return ProcedureResult{RunVerdict::Aborted, "Проверка остановлена", {}};
        const auto reply = context.equipment.invoke("operator.manual_input", "enter_measurement", {
            {"title", node.title}, {"prompt", node.arguments.at("instruction")}, {"unit", node.arguments.at("unit")}});
        if (context.stopRequested.load()) return ProcedureResult{RunVerdict::Aborted, "Проверка остановлена", {}};
        std::istringstream lines(reply);
        std::map<std::string, std::string> fields;
        std::string line;
        while (std::getline(lines, line)) {
            const auto separator = line.find('=');
            if (separator != std::string::npos && !fields.emplace(line.substr(0, separator), line.substr(separator + 1)).second)
                return ProcedureResult{RunVerdict::Error, "Повторное поле ответа оператора", {}};
        }
        if (fields["status"] != "confirmed") return ProcedureResult{RunVerdict::Error, "Ввод измерения отменён", {}};
        double value = 0.0;
        try {
            std::size_t used = 0;
            value = std::stod(fields.at("value"), &used);
            if (used != fields.at("value").size() || !std::isfinite(value)) throw std::invalid_argument("value");
        } catch (const std::exception&) {
            return ProcedureResult{RunVerdict::Error, "Введено некорректное измерение", {}};
        }
        MeasurementResult measurement;
        measurement.parameterKey = node.id;
        measurement.title = node.title;
        measurement.measured = value;
        measurement.lowerLimit = std::stod(node.arguments.at("lower"));
        measurement.upperLimit = std::stod(node.arguments.at("upper"));
        measurement.unit = node.arguments.at("unit");
        measurement.verdict = value >= measurement.lowerLimit && value <= measurement.upperLimit
            ? RunVerdict::Ok : RunVerdict::Fail;
        measurement.message = measurement.verdict == RunVerdict::Ok ? "В пределах допуска" : "Значение вне допуска";
        measurement.attributes["input_method"] = "manual";
        measurement.attributes["sample_time_ms"] = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        return ProcedureResult{measurement.verdict, measurement.message, {measurement}};
    }, validateMeasurement);
}

ScenarioRunResult ScenarioEngine::run(
    const ScenarioDefinition& scenario,
    ICapabilityProvider& equipment,
    std::string profileVersion,
    std::string objectSerial,
    bool allowPartial,
    std::function<void(const RunEvent&)> progressSink,
    ResourceLeaseManager* resourceLeases,
    std::function<void(const ScenarioRunResult&)> checkpointSink,
    const ScenarioRunResult* initialRun)
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        throw std::runtime_error("ScenarioEngine already has an active run");
    }
    struct RunningGuard final {
        std::atomic_bool& running;
        std::atomic_bool& stop;
        // Сбрасываем запрос только после завершения запуска: запрос, пришедший
        // между постановкой задачи в очередь и входом сюда, должен сохраниться.
        ~RunningGuard() { stop.store(false); running.store(false); }
    } runningGuard{running_, stopRequested_};

    if (initialRun && (!checkpointSink || !initialRun->persistenceStarted
        || initialRun->runId.empty() || initialRun->scenarioId != scenario.id
        || initialRun->scenarioVersion != scenario.version
        || initialRun->catalogVersion != scenario.catalogVersion
        || initialRun->profileVersion != profileVersion
        || initialRun->objectSerial != objectSerial
        || initialRun->finishedAt != std::chrono::system_clock::time_point{}
        || !initialRun->steps.empty()))
        throw std::invalid_argument("Несовместимый начальный снимок проверки");
    auto run = initialRun ? *initialRun
                          : makeInitialRun(scenario, std::move(profileVersion), std::move(objectSerial));

    const auto publish = [&run, &progressSink](RunEvent event) {
        run.events.push_back(event);
        if (progressSink) progressSink(event);
    };

    const auto finish = [&run] {
        run.finishedAt = std::chrono::system_clock::now();
    };

    // Подготовка могла выполняться до повторной валидации. Получаем резерв
    // заявленных ресурсов перед сбросом; при чужом резерве или требовании
    // восстановления этот запуск не имеет права обращаться к оборудованию.
    const auto stopAfterRejectedScenario = [&] {
        std::set<std::string> resources;
        collectRequiredResources(scenario.steps, resources);
        if (!resourceLeases || resources.empty()) {
            equipment.safeStopAll();
            return;
        }
        ResourceLeaseManager::Lease cleanupLease;
        try {
            cleanupLease = resourceLeases->acquire(run.runId, resources);
        } catch (const ResourceBusyError& error) {
            run.events.push_back({std::chrono::system_clock::now(), {}, "RESOURCE_LEASE",
                "Сброс не выполнен: ресурс занят другим запуском", RunVerdict::Incomplete,
                {{"resource", error.resource()}, {"owner", error.owner()}}});
            return;
        } catch (const ResourceRecoveryRequiredError& error) {
            run.events.push_back({std::chrono::system_clock::now(), {}, "RESOURCE_RECOVERY",
                "Сброс не выполнен: ресурс требует восстановления", RunVerdict::Incomplete,
                {{"resource", error.resource()}, {"reason", error.record().reason}}});
            return;
        }
        const auto results = equipment.safeStopResources(resources);
        for (const auto& resource : resources) {
            const auto result = results.find(resource);
            const bool confirmed = result != results.end() && result->second.confirmed;
            const auto reason = result == results.end()
                ? std::string("provider returned no safe-stop result for resource") : result->second.reason;
            // Отклонённая до исполнения методика не является аппаратным
            // отказом; при подтверждённом сбросе ресурс остаётся доступен.
            resourceLeases->recordSafeStop(resource, run.runId, confirmed, false,
                "scenario rejected; cleanup: " + reason);
        }
    };

    const auto validationErrors = validate(scenario);
    if (!validationErrors.empty()) {
        run.verdict = RunVerdict::Incomplete;
        for (const auto& error : validationErrors) {
            run.events.push_back({
                std::chrono::system_clock::now(), {}, "VALIDATION", error,
                RunVerdict::Incomplete});
        }
        // Прикладной слой мог подготовить оборудование до повторной
        // проверки сценария. Не оставляем его в активном состоянии.
        stopAfterRejectedScenario();
        finish();
        return run;
    }

    if (scenario.publicationState != PublicationState::Published && !allowPartial) {
        run.verdict = RunVerdict::Incomplete;
        run.events.push_back({
            std::chrono::system_clock::now(), {}, "VALIDATION",
            "Черновой сценарий нельзя использовать для приёмочного результата",
            RunVerdict::Incomplete});
        stopAfterRejectedScenario();
        finish();
        return run;
    }

    std::vector<ScenarioNode> executionSteps;
    try { executionSteps = expandRepeats(scenario.steps); }
    catch (const std::exception& error) {
        run.verdict = RunVerdict::Error;
        run.events.push_back({std::chrono::system_clock::now(), {}, "ENGINE", error.what(), RunVerdict::Error});
        stopAfterRejectedScenario();
        finish();
        return run;
    }

    // До первого воздействия фиксируем незавершённый запуск. При отказе
    // записи процедура не получает доступ к источнику и оборудованию.
    const auto checkpoint = [&](const ScenarioRunResult& snapshot) {
        if (!checkpointSink || run.persistenceFailed) return !run.persistenceFailed;
        try {
            checkpointSink(snapshot);
            return true;
        } catch (const std::exception& error) {
            run.persistenceFailed = true;
            run.persistenceError = error.what();
        } catch (...) {
            run.persistenceFailed = true;
            run.persistenceError = "Неизвестная ошибка записи";
        }
        return false;
    };
    // A delivery declares its logical resources in ScenarioNode::requiredResources.
    // Claim the complete set once, before the first physical procedure. Keeping
    // the lease alive through engine.run() also keeps it alive through the
    // cleanup and resource-state recording at the end of this function.
    ResourceLeaseManager::Lease lease;
    std::set<std::string> leasedResources;
    if (resourceLeases) {
        collectRequiredResources(scenario.steps, leasedResources);
        if (!leasedResources.empty()) {
            try {
                lease = resourceLeases->acquire(run.runId, leasedResources);
            } catch (const ResourceBusyError& error) {
                run.verdict = RunVerdict::Incomplete;
                publish({
                    std::chrono::system_clock::now(), {}, "RESOURCE_LEASE",
                    "Запуск не начат: ресурс станции занят другим запуском",
                    RunVerdict::Incomplete,
                    {{"resource", error.resource()}, {"owner", error.owner()}}});
                finish();
                return run;
            } catch (const ResourceRecoveryRequiredError& error) {
                run.verdict = RunVerdict::Incomplete;
                publish({
                    std::chrono::system_clock::now(), {}, "RESOURCE_RECOVERY",
                    "Запуск не начат: требуется восстановление ресурса",
                    RunVerdict::Incomplete,
                    {{"resource", error.resource()},
                     {"state", toString(error.record().state)},
                     {"reason", error.record().reason}}});
                finish();
                return run;
            }
            try {
                publish({
                    std::chrono::system_clock::now(), {}, "RESOURCE_LEASE",
                    "Ресурсы станции закреплены за запуском", RunVerdict::NotRun,
                    {{"owner", lease.owner()},
                     {"resource_count", std::to_string(lease.resources().size())}}});
            } catch (...) {
                // Подготовка могла включить оборудование до вызова движка.
                // Даже ошибка подписчика не должна освободить резерв без сброса
                // и фиксации неподтверждённого состояния всех его ресурсов.
                const auto results = equipment.safeStopResources(leasedResources);
                for (const auto& resource : leasedResources) {
                    const auto result = results.find(resource);
                    const bool confirmed = result != results.end() && result->second.confirmed;
                    const std::string reason = result == results.end()
                        ? "provider returned no safe-stop result for resource" : result->second.reason;
                    // Сбой подписчика не свидетельствует об отказе прибора;
                    // неподтверждённый сброс всё равно остаётся Indeterminate.
                    resourceLeases->recordSafeStop(resource, run.runId, confirmed, false,
                        "progress observer failed; cleanup: " + reason);
                }
                throw;
            }
        } else {
            publish({
                std::chrono::system_clock::now(), {}, "RESOURCE_LEASE",
                "Сценарий не объявляет логические ресурсы; межзапусковая блокировка не применена",
                RunVerdict::NotRun});
        }
    }

    // Ресурс сначала закрепляется за запуском: ошибка записи не должна
    // останавливать прибор, который уже занят другим владельцем. Резерв
    // удерживается до сброса и регистрации фактического состояния ресурса.
    const auto stopLeasedResources = [&](bool operationFailed, const std::string& failureReason) {
        const auto stopResults = equipment.safeStopResources(leasedResources);
        bool allConfirmed = true;
        std::vector<RunEvent> stopEvents;
        for (const auto& resource : leasedResources) {
            const auto result = stopResults.find(resource);
            const bool confirmed = result != stopResults.end() && result->second.confirmed;
            std::string reason = result == stopResults.end()
                ? "provider returned no safe-stop result for resource" : result->second.reason;
            if (!failureReason.empty()) reason = "run failure: " + failureReason + "; cleanup: " + reason;
            resourceLeases->recordSafeStop(resource, run.runId, confirmed, operationFailed, reason);
            const auto state = !confirmed ? "INDETERMINATE" : operationFailed ? "ERROR" : "SAFE";
            stopEvents.push_back({std::chrono::system_clock::now(), {}, "RESOURCE_SAFE_STOP",
                confirmed ? "Безопасная остановка ресурса подтверждена"
                          : "Безопасная остановка ресурса не подтверждена",
                confirmed ? RunVerdict::NotRun : RunVerdict::Error,
                {{"resource", resource}, {"state", state}, {"reason", reason}}});
            allConfirmed = allConfirmed && confirmed;
        }
        // Сначала сохраняем состояние всех ресурсов, затем уведомляем UI:
        // исключение обработчика события не должно оставить часть записей ACTIVE.
        for (auto& event : stopEvents) publish(std::move(event));
        return allConfirmed;
    };

    auto initial = run;
    initial.verdict = RunVerdict::Incomplete;
    if (!checkpoint(initial)) {
        run.verdict = RunVerdict::Error;
        run.events.push_back({std::chrono::system_clock::now(), {}, "PERSISTENCE_ERROR",
                              run.persistenceError, RunVerdict::Error});
        // Ошибка записи относится к хранилищу. При подтверждённом сбросе
        // нельзя объявлять исправное оборудование неисправным.
        if (resourceLeases && lease) stopLeasedResources(false, run.persistenceError);
        else equipment.safeStopAll();
        finish();
        return run;
    }
    run.persistenceStarted = static_cast<bool>(checkpointSink);

    std::map<std::string, StepRunResult> completedSteps;
    std::function<std::optional<StepRunResult>(const ScenarioNode&)> partialNode;
    partialNode = [&](const ScenarioNode& node) -> std::optional<StepRunResult> {
        if (node.children.empty()) {
            const auto found = completedSteps.find(node.id);
            return found == completedSteps.end() ? std::nullopt
                                                  : std::optional<StepRunResult>(found->second);
        }
        StepRunResult group;
        group.nodeId = node.id;
        group.definitionId = node.definitionId;
        group.title = node.title;
        group.tuRequirement = node.tuRequirement;
        group.verdict = RunVerdict::Incomplete;
        group.message = "Группа не завершена";
        for (const auto& child : node.children) {
            auto partial = partialNode(child);
            if (partial) group.children.push_back(std::move(*partial));
        }
        if (group.children.empty()) return std::nullopt;
        if (group.children.size() == node.children.size()) {
            group.verdict = RunVerdict::Ok;
            for (const auto& child : group.children)
                group.verdict = combineVerdicts(group.verdict, child.verdict);
            group.message = group.verdict == RunVerdict::Ok
                ? "Все вложенные проверки выполнены"
                : "Группа содержит проверки без результата или с отклонениями";
        }
        return group;
    };
    ProcedureContext context{equipment, stopRequested_, publish, run.runId, {}, {}};
    if (checkpointSink) {
        context.stepCompleted = [&](const StepRunResult& step) {
            completedSteps[step.nodeId] = step;
            auto partial = run;
            // В промежуточном снимке сохраняем путь каждого законченного
            // шага, чтобы после прерывания восстановить его группу и порядок.
            partial.steps.clear();
            for (const auto& root : executionSteps) {
                auto node = partialNode(root);
                if (node) partial.steps.push_back(std::move(*node));
            }
            partial.verdict = RunVerdict::Incomplete;
            partial.finishedAt = {};
            if (!checkpoint(partial)) {
                // Измерение остаётся в памяти результата, но дальнейшие шаги
                // не запускаются после отказа записи контрольной точки.
                stopRequested_.store(true);
            }
        };
    }

    run.verdict = RunVerdict::Ok;
    std::string operationFailureReason;
    try {
        for (const auto& node : executionSteps) {
            auto step = runNode(node, context, allowPartial);
            const auto stepVerdict = step.verdict;
            if (stepVerdict == RunVerdict::Error || stepVerdict == RunVerdict::Fail) {
                operationFailureReason = step.message.empty()
                    ? "scenario step failed: " + node.id : step.message;
            }
            run.verdict = combineVerdicts(run.verdict, stepVerdict);
            run.steps.push_back(std::move(step));
            if (run.persistenceFailed || shouldStop(stepVerdict, allowPartial)) break;
        }
    } catch (const std::exception& error) {
        run.verdict = RunVerdict::Error;
        operationFailureReason = error.what();
        run.events.push_back({
            std::chrono::system_clock::now(), {}, "ENGINE", error.what(),
            RunVerdict::Error});
    } catch (...) {
        run.verdict = RunVerdict::Error;
        operationFailureReason = "Неизвестная ошибка сценарного движка";
        run.events.push_back({
            std::chrono::system_clock::now(), {}, "ENGINE",
            "Неизвестная ошибка сценарного движка", RunVerdict::Error});
    }

    if (resourceLeases && lease) {
        const bool allConfirmed = stopLeasedResources(run.verdict == RunVerdict::Error,
                                                       operationFailureReason);
        if (!allConfirmed && run.verdict == RunVerdict::Ok)
            run.verdict = RunVerdict::Error;
    } else {
        equipment.safeStopAll();
    }
    if (allowPartial && run.verdict == RunVerdict::Ok) {
        run.verdict = RunVerdict::Incomplete;
    }
    finish();
    if (run.persistenceFailed) {
        run.verdict = RunVerdict::Error;
        run.events.push_back({std::chrono::system_clock::now(), {}, "PERSISTENCE_ERROR",
                              run.persistenceError, RunVerdict::Error});
    } else if (!checkpoint(run)) {
        run.verdict = RunVerdict::Error;
        run.events.push_back({std::chrono::system_clock::now(), {}, "PERSISTENCE_ERROR",
                              run.persistenceError, RunVerdict::Error});
    }
    return run;
}

void ScenarioEngine::requestStop() noexcept
{
    stopRequested_.store(true);
}

void ScenarioEngine::resetStop() noexcept
{
    // Do not allow a second UI/workflow to clear the stop request of the
    // currently executing run. run() resets the flag after it claims execution.
    if (!running_.load()) stopRequested_.store(false);
}

} // namespace orbita::stand
