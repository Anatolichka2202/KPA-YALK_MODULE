#include "backend/scenario_engine.h"

#include <chrono>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <utility>

namespace tu {
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

int verdictRank(RunVerdict verdict) noexcept
{
    switch (verdict) {
    case RunVerdict::NotRun: return 0;
    case RunVerdict::Ok: return 1;
    case RunVerdict::Incomplete: return 2;
    case RunVerdict::Fail: return 3;
    case RunVerdict::Error: return 4;
    case RunVerdict::Aborted: return 5;
    }
    return 4;
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
    return verdictRank(next) > verdictRank(current) ? next : current;
}

void ScenarioEngine::registerProcedure(std::string id, ProcedureFunction procedure)
{
    if (id.empty() || !procedure)
        throw std::invalid_argument("Procedure id and callback are required");
    if (procedures_.count(id))
        throw std::logic_error("Duplicate procedure registration: " + id);
    procedures_.emplace(std::move(id), std::move(procedure));
}

std::vector<std::string> ScenarioEngine::validate(const ScenarioDefinition& scenario) const
{
    std::vector<std::string> errors;
    if (scenario.id.empty()) errors.emplace_back("У сценария отсутствует id");
    if (scenario.title.empty()) errors.emplace_back("У сценария отсутствует название");
    if (scenario.version.empty()) errors.emplace_back("У сценария отсутствует версия");
    if (scenario.steps.empty()) errors.emplace_back("Сценарий не содержит шагов");

    std::set<std::string> ids;
    for (const auto& step : scenario.steps) {
        if (step.id.empty()) errors.emplace_back("У шага отсутствует id");
        else if (!ids.insert(step.id).second)
            errors.emplace_back("Повторяющийся id шага: " + step.id);
        if (step.title.empty()) errors.emplace_back("У шага " + step.id + " отсутствует название");
        if (step.tuRequirement.empty())
            errors.emplace_back("У шага " + step.id + " отсутствует ссылка на ТУ/методику");
        if (step.procedure.empty())
            errors.emplace_back("У шага " + step.id + " отсутствует процедура");
        else if (!procedures_.count(step.procedure))
            errors.emplace_back("Не зарегистрирована процедура " + step.procedure + " для шага " + step.id);
    }
    return errors;
}

void ScenarioEngine::emit(ProcedureContext& context, const RunEvent& event) const
{
    if (context.eventSink) context.eventSink(event);
}

StepRunResult ScenarioEngine::runStep(const ScenarioStep& step, ProcedureContext& context)
{
    StepRunResult result;
    result.nodeId = step.id;
    result.title = step.title;
    result.tuRequirement = step.tuRequirement;

    if (stopRequested_.load()) {
        result.verdict = RunVerdict::Aborted;
        result.message = "Остановлено оператором";
        return result;
    }

    emit(context, {std::chrono::system_clock::now(), step.id, "START",
                   step.title, RunVerdict::NotRun, {}});

    try {
        context.checkpoint();
        const auto procedure = procedures_.find(step.procedure);
        if (procedure == procedures_.end()) {
            result.verdict = RunVerdict::Error;
            result.message = "Процедура не зарегистрирована: " + step.procedure;
        } else {
            auto procedureResult = procedure->second(step, context);
            context.checkpoint();
            result.verdict = procedureResult.verdict;
            result.message = std::move(procedureResult.message);
            result.measurements = std::move(procedureResult.measurements);
        }
    } catch (const StepSkipped&) {
        result.verdict = RunVerdict::Ok;
        result.operatorSkipped = true;
        result.message = "НОРМА · проверка пропущена оператором (Ctrl+Shift+Q)";
        emit(context, {std::chrono::system_clock::now(), step.id, "SKIPPED",
                       result.message, RunVerdict::Ok,
                       {{"operator_skipped", "true"}, {"shortcut", "Ctrl+Shift+Q"}}});
    } catch (const RunStopped&) {
        result.verdict = RunVerdict::Aborted;
        result.message = "Остановлено оператором";
    } catch (const std::exception& error) {
        result.verdict = RunVerdict::Error;
        result.message = error.what();
    } catch (...) {
        result.verdict = RunVerdict::Error;
        result.message = "Неизвестная ошибка процедуры";
    }

    skipRequested_.store(false);
    emit(context, {std::chrono::system_clock::now(), step.id, "FINISH",
                   result.message, result.verdict,
                   result.operatorSkipped
                       ? std::map<std::string, std::string>{{"operator_skipped", "true"}}
                       : std::map<std::string, std::string>{}});
    return result;
}

ScenarioRunResult ScenarioEngine::run(
    const ScenarioDefinition& scenario,
    std::string objectSerial,
    std::function<void(const RunEvent&)> progressSink)
{
    resetControl();

    ScenarioRunResult run;
    run.runId = newRunId();
    run.scenarioId = scenario.id;
    run.scenarioTitle = scenario.title;
    run.scenarioVersion = scenario.version;
    run.objectSerial = std::move(objectSerial);
    run.startedAt = std::chrono::system_clock::now();

    const auto validationErrors = validate(scenario);
    if (!validationErrors.empty()) {
        run.verdict = RunVerdict::Incomplete;
        for (const auto& error : validationErrors) {
            RunEvent event{std::chrono::system_clock::now(), {}, "VALIDATION",
                           error, RunVerdict::Incomplete, {}};
            run.events.push_back(event);
            if (progressSink) progressSink(event);
        }
        run.finishedAt = std::chrono::system_clock::now();
        return run;
    }

    ProcedureContext context{
        stopRequested_,
        skipRequested_,
        [&run, &progressSink](const RunEvent& event) {
            run.events.push_back(event);
            if (progressSink) progressSink(event);
        },
        run.runId,
        {},
    };

    run.verdict = RunVerdict::Ok;
    for (const auto& step : scenario.steps) {
        auto stepResult = runStep(step, context);
        run.verdict = combineVerdicts(run.verdict, stepResult.verdict);
        const bool terminal = stepResult.verdict == RunVerdict::Error
            || stepResult.verdict == RunVerdict::Aborted;
        run.steps.push_back(std::move(stepResult));
        if (terminal) break;
    }

    run.finishedAt = std::chrono::system_clock::now();
    return run;
}

void ScenarioEngine::requestStop() noexcept
{
    stopRequested_.store(true);
}

void ScenarioEngine::requestSkipCurrent() noexcept
{
    skipRequested_.store(true);
}

void ScenarioEngine::resetControl() noexcept
{
    stopRequested_.store(false);
    skipRequested_.store(false);
}

} // namespace tu
