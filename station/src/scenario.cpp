#include "orbita_stand/scenario.h"

#include <algorithm>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

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

// Programmatic schema-1 tests still construct ScenarioNode aggregates with the
// old arguments key. YAML-loaded scenarios never take this path because the
// loader consumes technical_retries into the typed policy.
bool retryLimit(const ScenarioNode& node, unsigned& value)
{
    value = node.policy.technicalRetries;
    if (value > 3) return false;
    if (value != 0) return true;

    const auto legacy = node.arguments.find("technical_retries");
    if (legacy == node.arguments.end()) return true;
    try {
        std::size_t parsed = 0;
        const auto retries = std::stoull(legacy->second, &parsed);
        if (parsed != legacy->second.size() || retries > 3) return false;
        value = static_cast<unsigned>(retries);
        return true;
    } catch (...) {
        return false;
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
        if (node.tuRequirement.empty()) {
            errors.emplace_back("У конечного шага " + node.id + " отсутствует ссылка на пункт ТУ");
        }
    }

    for (const auto& child : node.children) {
        validateNode(child, procedures, ids, errors);
    }
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

void ScenarioEngine::registerProcedure(std::string id, ProcedureFunction procedure)
{
    if (id.empty() || !procedure) {
        throw std::invalid_argument("Procedure id and callback are required");
    }
    procedures_[std::move(id)] = std::move(procedure);
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
        validateNode(node, procedures_, ids, errors);
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
                ProcedureContext scopedContext{
                    scopedEquipment,
                    context.stopRequested,
                    context.eventSink,
                    context.runId,
                    std::move(context.state),
                };

                unsigned retries = 0;
                retryLimit(node, retries); // validate() already checked it.
                for (unsigned attempt = 0; attempt <= retries; ++attempt) {
                    try {
                        auto procedureResult = procedure->second(node, scopedContext);
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
    return result;
}

ScenarioRunResult ScenarioEngine::run(
    const ScenarioDefinition& scenario,
    ICapabilityProvider& equipment,
    std::string profileVersion,
    std::string objectSerial,
    bool allowPartial,
    std::function<void(const RunEvent&)> progressSink)
{
    // A ScenarioEngine represents one sequential station runner. A previous
    // operator stop must not poison the next run.
    stopRequested_.store(false);

    ScenarioRunResult run;
    run.runId = newRunId();
    run.scenarioId = scenario.id;
    run.scenarioTitle = scenario.title;
    run.scenarioVersion = scenario.version;
    run.catalogVersion = scenario.catalogVersion;
    run.profileVersion = std::move(profileVersion);
    run.objectSerial = std::move(objectSerial);
    run.startedAt = std::chrono::system_clock::now();

    const auto finish = [&run] {
        run.finishedAt = std::chrono::system_clock::now();
    };

    const auto validationErrors = validate(scenario);
    if (!validationErrors.empty()) {
        run.verdict = RunVerdict::Incomplete;
        for (const auto& error : validationErrors) {
            run.events.push_back({
                std::chrono::system_clock::now(), {}, "VALIDATION", error,
                RunVerdict::Incomplete});
        }
        finish();
        return run;
    }

    if (scenario.publicationState != PublicationState::Published && !allowPartial) {
        run.verdict = RunVerdict::Incomplete;
        run.events.push_back({
            std::chrono::system_clock::now(), {}, "VALIDATION",
            "Черновой сценарий нельзя использовать для приёмочного результата",
            RunVerdict::Incomplete});
        finish();
        return run;
    }

    ProcedureContext context{
        equipment,
        stopRequested_,
        [&run, &progressSink](const RunEvent& event) {
            run.events.push_back(event);
            if (progressSink) progressSink(event);
        },
        run.runId,
        {},
    };

    run.verdict = RunVerdict::Ok;
    try {
        for (const auto& node : scenario.steps) {
            auto step = runNode(node, context, allowPartial);
            const auto stepVerdict = step.verdict;
            run.verdict = combineVerdicts(run.verdict, stepVerdict);
            run.steps.push_back(std::move(step));
            if (shouldStop(stepVerdict, allowPartial)) break;
        }
    } catch (const std::exception& error) {
        run.verdict = RunVerdict::Error;
        run.events.push_back({
            std::chrono::system_clock::now(), {}, "ENGINE", error.what(),
            RunVerdict::Error});
    } catch (...) {
        run.verdict = RunVerdict::Error;
        run.events.push_back({
            std::chrono::system_clock::now(), {}, "ENGINE",
            "Неизвестная ошибка сценарного движка", RunVerdict::Error});
    }

    equipment.safeStopAll();
    if (allowPartial && run.verdict == RunVerdict::Ok) {
        run.verdict = RunVerdict::Incomplete;
    }
    finish();
    return run;
}

void ScenarioEngine::requestStop() noexcept
{
    stopRequested_.store(true);
}

void ScenarioEngine::resetStop() noexcept
{
    stopRequested_.store(false);
}

} // namespace orbita::stand
