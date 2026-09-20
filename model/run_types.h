#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace tu {

enum class RunVerdict {
    NotRun,
    Ok,
    Fail,
    Incomplete,
    Error,
    Aborted,
};

struct MeasurementResult {
    std::string parameterKey;
    std::string title;
    double reference = 0.0;
    double measured = 0.0;
    double lowerLimit = 0.0;
    double upperLimit = 0.0;
    std::string unit;
    RunVerdict verdict = RunVerdict::Error;
    std::string message;
    std::map<std::string, std::string> attributes;
};

struct ProcedureResult {
    RunVerdict verdict = RunVerdict::Error;
    std::string message;
    std::vector<MeasurementResult> measurements;
};

struct RunEvent {
    std::chrono::system_clock::time_point timestamp{};
    std::string nodeId;
    std::string stage;
    std::string message;
    RunVerdict verdict = RunVerdict::NotRun;
    std::map<std::string, std::string> data;
};

struct StepRunResult {
    std::string nodeId;
    std::string title;
    std::string tuRequirement;
    RunVerdict verdict = RunVerdict::NotRun;
    std::string message;
    std::vector<MeasurementResult> measurements;
    std::vector<StepRunResult> children;
    bool operatorSkipped = false;
};

struct ScenarioRunResult {
    std::string runId;
    std::string scenarioId;
    std::string scenarioTitle;
    std::string scenarioVersion;
    std::string objectSerial;
    std::chrono::system_clock::time_point startedAt{};
    std::chrono::system_clock::time_point finishedAt{};
    RunVerdict verdict = RunVerdict::NotRun;
    std::vector<StepRunResult> steps;
    std::vector<RunEvent> events;
};

const char* toString(RunVerdict verdict) noexcept;
RunVerdict combineVerdicts(RunVerdict current, RunVerdict next) noexcept;

} // namespace tu
