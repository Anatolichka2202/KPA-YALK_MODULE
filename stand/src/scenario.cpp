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

bool isTerminalError(RunVerdict verdict)
{
    return verdict == RunVerdict::Error || verdict == RunVerdict::Aborted;
}

void validateNode(
    const ScenarioNode& node,
    const std::map<std::string, ProcedureFunction>& procedures,
    std::set<std::string>& ids,
    std::vector<std::string>& errors)
{
    if (node.id.empty()) errors.emplace_back("У шага отсутствует id");
    else if (!ids.insert(node.id).second) errors.emplace_back("Повторяющийся id шага: " + node.id);
    if (node.title.empty()) errors.emplace_back("У шага " + node.id + " отсутствует название");
    if (!node.children.empty() && !node.procedure.empty()) {
        errors.emplace_back("Шаг " + node.id + " не может одновременно быть процедурой и группой");
    }
    if (node.children.empty()) {
        if (node.procedure.empty()) errors.emplace_back("У конечного шага " + node.id + " отсутствует процедура");
        else if (!procedures.count(node.procedure)) {
            errors.emplace_back("Не зарегистрирована процедура " + node.procedure + " для шага " + node.id);
        }
        if (node.tuRequirement.empty()) {
            errors.emplace_back("У конечного шага " + node.id + " отсутствует ссылка на пункт ТУ");
        }
    }
    for (const auto& child : node.children) validateNode(child, procedures, ids, errors);
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
        // Если хотя бы одно выполненное измерение не прошло допуск, общий
        // результат обязан оставаться FAIL. Признак commissioning/частичного
        // состава не должен маскировать уже установленное несоответствие.
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
    if (id.empty() || !procedure) throw std::invalid_argument("Procedure id and callback are required");
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
    for (const auto& node : scenario.steps) validateNode(node, procedures_, ids, errors);
    return errors;
}

StepRunResult ScenarioEngine::runNode(
    const ScenarioNode& node,
    ProcedureContext& context,
    bool allowPartial,
    bool& stopTraversal)
{
    StepRunResult result;
    result.nodeId = node.id;
    result.title = node.title;
    result.tuRequirement = node.tuRequirement;

    if (context.stopRequested.load()) {
        result.verdict = RunVerdict::Aborted;
        result.message = "Остановлено оператором";
        stopTraversal = true;
        return result;
    }

    context.eventSink({std::chrono::system_clock::now(), node.id, "START", node.title, RunVerdict::NotRun});

    if (!node.children.empty()) {
        result.verdict = RunVerdict::Ok;
        for (const auto& child : node.children) {
            auto childResult = runNode(child, context, allowPartial, stopTraversal);
            result.verdict = combineVerdicts(result.verdict, childResult.verdict);
            result.children.push_back(std::move(childResult));
            if (stopTraversal) break;
        }
        result.message = result.verdict == RunVerdict::Ok
            ? "Все вложенные проверки выполнены"
            : "Группа содержит проверки без результата или с отклонениями";
    } else {
        std::vector<std::string> missing;
        for (const auto& capability : node.requiredCapabilities) {
            if (!context.equipment.hasCapability(capability)) missing.push_back(capability);
        }
        if (!missing.empty()) {
            std::ostringstream message;
            message << "Недоступны возможности: ";
            for (std::size_t i = 0; i < missing.size(); ++i) {
                if (i) message << ", ";
                message << missing[i];
            }
            result.verdict = RunVerdict::Incomplete;
            result.message = message.str();
            if (!allowPartial) stopTraversal = true;
        } else {
            try {
                const auto procedure = procedures_.find(node.procedure);
                if (procedure == procedures_.end()) {
                    result.verdict = RunVerdict::Incomplete;
                    result.message = "Процедура не зарегистрирована: " + node.procedure;
                    if (!allowPartial) stopTraversal = true;
                } else {
                    auto procedureResult = procedure->second(node, context);
                    result.verdict = procedureResult.verdict;
                    result.message = std::move(procedureResult.message);
                    result.measurements = std::move(procedureResult.measurements);
                    if (isTerminalError(result.verdict)) stopTraversal = true;
                }
            } catch (const std::exception& error) {
                result.verdict = RunVerdict::Error;
                result.message = error.what();
                stopTraversal = true;
            } catch (...) {
                result.verdict = RunVerdict::Error;
                result.message = "Неизвестная ошибка процедуры";
                stopTraversal = true;
            }
        }
    }

    context.eventSink({std::chrono::system_clock::now(), node.id, "FINISH", result.message, result.verdict});
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
    ScenarioRunResult run;
    run.runId = newRunId();
    run.scenarioId = scenario.id;
    run.scenarioTitle = scenario.title;
    run.scenarioVersion = scenario.version;
    run.catalogVersion = scenario.catalogVersion;
    run.profileVersion = std::move(profileVersion);
    run.objectSerial = std::move(objectSerial);
    run.startedAt = std::chrono::system_clock::now();

    const auto validationErrors = validate(scenario);
    if (!validationErrors.empty()) {
        run.verdict = RunVerdict::Incomplete;
        for (const auto& error : validationErrors) {
            run.events.push_back({std::chrono::system_clock::now(), {}, "VALIDATION", error,
                                  RunVerdict::Incomplete});
        }
        run.finishedAt = std::chrono::system_clock::now();
        return run;
    }
    if (scenario.publicationState != PublicationState::Published && !allowPartial) {
        run.verdict = RunVerdict::Incomplete;
        run.events.push_back({std::chrono::system_clock::now(), {}, "VALIDATION",
                              "Черновой сценарий нельзя использовать для приёмочного результата",
                              RunVerdict::Incomplete});
        run.finishedAt = std::chrono::system_clock::now();
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
    bool stopTraversal = false;
    try {
        for (const auto& node : scenario.steps) {
            auto step = runNode(node, context, allowPartial, stopTraversal);
            run.verdict = combineVerdicts(run.verdict, step.verdict);
            run.steps.push_back(std::move(step));
            if (stopTraversal) break;
        }
    } catch (...) {
        run.verdict = RunVerdict::Error;
        stopTraversal = true;
    }
    // Испытательное воздействие всегда завершается безопасным сбросом — в том
    // числе после полностью успешного прогона.
    equipment.safeStopAll();
    if (allowPartial && run.verdict == RunVerdict::Ok) run.verdict = RunVerdict::Incomplete;
    run.finishedAt = std::chrono::system_clock::now();
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
