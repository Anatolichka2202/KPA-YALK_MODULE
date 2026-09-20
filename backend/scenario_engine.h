#pragma once

#include "model/run_types.h"

#include <atomic>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace tu {

struct ScenarioStep {
    std::string id;
    std::string title;
    std::string tuRequirement;
    std::string procedure;
    std::map<std::string, std::string> arguments;
};

struct ScenarioDefinition {
    std::string id;
    std::string title;
    std::string version;
    std::vector<ScenarioStep> steps;
};

class StepSkipped final : public std::runtime_error {
public:
    StepSkipped() : std::runtime_error("Проверка пропущена оператором") {}
};

class RunStopped final : public std::runtime_error {
public:
    RunStopped() : std::runtime_error("Остановлено оператором") {}
};

struct ProcedureContext {
    std::atomic_bool& stopRequested;
    std::atomic_bool& skipRequested;
    std::function<void(const RunEvent&)> eventSink;
    std::string runId;
    std::map<std::string, std::string> state;

    void checkpoint() const
    {
        if (stopRequested.load()) throw RunStopped{};
        if (skipRequested.load()) throw StepSkipped{};
    }
};

using ProcedureFunction = std::function<ProcedureResult(
    const ScenarioStep&, ProcedureContext&)>;

class ScenarioEngine final {
public:
    void registerProcedure(std::string id, ProcedureFunction procedure);
    std::vector<std::string> validate(const ScenarioDefinition& scenario) const;

    ScenarioRunResult run(
        const ScenarioDefinition& scenario,
        std::string objectSerial,
        std::function<void(const RunEvent&)> progressSink = {});

    void requestStop() noexcept;
    void requestSkipCurrent() noexcept;
    void resetControl() noexcept;

private:
    StepRunResult runStep(const ScenarioStep& step, ProcedureContext& context);
    void emit(ProcedureContext& context, const RunEvent& event) const;

    std::map<std::string, ProcedureFunction> procedures_;
    std::atomic_bool stopRequested_{false};
    std::atomic_bool skipRequested_{false};
};

} // namespace tu
