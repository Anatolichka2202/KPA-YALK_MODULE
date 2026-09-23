#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace orbita::stand {

enum class PublicationState {
    Draft,
    Published,
};

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

// Explicit station-resource requirement. `resource` identifies the logical
// component/role selected by the delivery profile; `capability` describes the
// contract that this particular resource must implement. This removes the
// assumption that one capability uniquely identifies one physical device.
struct ResourceRequirement {
    std::string resource;
    std::string capability;
};

struct StepExecutionPolicy {
    unsigned technicalRetries = 0;
};

struct ScenarioNode {
    std::string id;
    std::string title;
    std::string tuRequirement;
    std::string procedure;
    std::set<std::string> requiredCapabilities;
    std::map<std::string, std::string> arguments;
    std::vector<ScenarioNode> children;

    // Appended after the legacy aggregate fields deliberately: existing C++
    // scenario initializers keep their meaning while scenarios migrate from
    // capability-only routing to explicit delivery resource roles.
    std::vector<ResourceRequirement> requiredResources;

    // Runtime policy is separate from procedure arguments. Schema-1 YAML still
    // accepts technical_retries inside args for compatibility; the loader
    // consumes it into this typed policy before procedures see the arguments.
    StepExecutionPolicy policy;
};

struct ScenarioDefinition {
    std::string id;
    std::string title;
    std::string version;
    std::string catalogVersion;
    std::string objectType;
    PublicationState publicationState = PublicationState::Draft;
    std::vector<ScenarioNode> steps;
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
};

struct ScenarioRunResult {
    std::string runId;
    std::string scenarioId;
    std::string scenarioTitle;
    std::string scenarioVersion;
    std::string catalogVersion;
    std::string profileVersion;
    std::string objectSerial;
    std::chrono::system_clock::time_point startedAt{};
    std::chrono::system_clock::time_point finishedAt{};
    RunVerdict verdict = RunVerdict::NotRun;
    std::vector<StepRunResult> steps;
    std::vector<RunEvent> events;

    // Product-level identity is appended so legacy ScenarioEngine callers keep
    // their current API. Project/workflow execution fills these fields at the
    // composition boundary; a raw scenario run legitimately leaves them empty.
    std::string projectId;
    std::string projectVersion;
    std::string workflowId;
    std::string dutType;
    std::string dutId;
    std::string operatorName;
    std::string environmentProfile;
    std::map<std::string, std::string> contextAttributes;
};

class ICapabilityProvider {
public:
    virtual ~ICapabilityProvider() = default;

    // Legacy/default routing. Retained while existing scenarios are migrated.
    virtual bool hasCapability(const std::string& capability) const = 0;
    virtual std::string invoke(
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments) = 0;

    // Resource-aware routing. Default implementations keep old providers
    // source-compatible; a provider that supports logical resources overrides
    // these methods. Procedures may then address two devices implementing the
    // same capability without relying on ambiguous global routing.
    virtual bool resourceHasCapability(
        const std::string& resource,
        const std::string& capability) const
    {
        (void)resource;
        (void)capability;
        return false;
    }

    virtual std::string invokeResource(
        const std::string& resource,
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments)
    {
        (void)capability;
        (void)operation;
        (void)arguments;
        throw std::runtime_error("Equipment resource routing is unavailable: " + resource);
    }

    virtual void safeStopAll() noexcept = 0;
};

struct ProcedureContext {
    ICapabilityProvider& equipment;
    std::atomic_bool& stopRequested;
    std::function<void(const RunEvent&)> eventSink;
    std::string runId;
    std::map<std::string, std::string> state;
};

using ProcedureFunction = std::function<ProcedureResult(
    const ScenarioNode&, ProcedureContext&)>;

class ScenarioEngine final {
public:
    void registerProcedure(std::string id, ProcedureFunction procedure);
    std::vector<std::string> validate(const ScenarioDefinition& scenario) const;
    ScenarioRunResult run(
        const ScenarioDefinition& scenario,
        ICapabilityProvider& equipment,
        std::string profileVersion,
        std::string objectSerial,
        bool allowPartial,
        std::function<void(const RunEvent&)> progressSink = {});
    void requestStop() noexcept;
    void resetStop() noexcept;
    bool running() const noexcept { return running_.load(); }

private:
    StepRunResult runNode(
        const ScenarioNode& node,
        ProcedureContext& context,
        bool allowPartial);

    std::map<std::string, ProcedureFunction> procedures_;
    std::atomic_bool stopRequested_{false};
    std::atomic_bool running_{false};
};

const char* toString(RunVerdict verdict) noexcept;
RunVerdict combineVerdicts(RunVerdict current, RunVerdict next) noexcept;

} // namespace orbita::stand
