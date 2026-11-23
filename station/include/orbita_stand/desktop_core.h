#pragma once

#include "orbita_stand/config.h"
#include "orbita_stand/project.h"
#include "orbita_stand/telemetry.h"

#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace orbita::stand::desktop {

// The desktop contract uses C++17 domain DTOs, not QWidget or a device driver.
// This contract is deliberately NOT an equipment invocation API.
enum class Edition { Engineering, Tu };
enum class RuntimeState { Unconfigured, Idle, Busy, Stopping, Fault };
enum class ComponentState { Disabled, NotChecked, Ready, Disconnected, Fault, Simulated };
enum class ParameterState {
    NoData, Fresh, Stale, Disconnected, Invalid, UnknownUnit, UnitMismatch
};
enum class CommandKind { StartWorkflow, RequestStop, CreateScenarioDraft };

const char* toString(Edition value) noexcept;
const char* toString(RuntimeState value) noexcept;
const char* toString(ComponentState value) noexcept;
const char* toString(ParameterState value) noexcept;
const char* toString(CommandKind value) noexcept;

struct ComponentObservation {
    ComponentState state = ComponentState::NotChecked;
    std::string diagnostic;
    // The source must supply real observation time. No timestamp -> not checked.
    std::optional<std::chrono::system_clock::time_point> observedAt;
};

struct RuntimeObservation {
    bool configured = false;
    bool busy = false;
    bool stopping = false;
    bool fault = false;
    bool readinessConfirmed = false;
    bool simulated = false;
    bool dutRegistered = false;
    bool environmentConfirmed = false;
    std::map<std::string, ComponentObservation> components;
    std::string activeRunId;
    std::optional<RunVerdict> lastRunVerdict;
};

struct DesktopPolicy {
    Edition edition = Edition::Engineering;
    // Tokens correspond to registered, explicitly supported host handlers.
    // An operator_action string in a project is NOT sufficient authorization.
    std::set<std::string> supportedActions;
    // TU builds expose exactly one selected workflow; no implicit fallback.
    std::string tuWorkflowId;
    bool allowScenarioEditing = false;
    // A TU build defaults to rejecting simulated execution.
    bool allowSimulationInTu = false;
};

struct CommandRequest {
    CommandKind kind = CommandKind::StartWorkflow;
    std::string workflowId;
};

struct Decision {
    bool allowed = false;
    std::string reason;
};

struct WorkflowView {
    std::string id;
    std::string title;
    std::string kind;
    std::string action;
    bool launchable = false;
    bool draftCreationAvailable = false;
    std::string unavailableReason;
};

struct ComponentView {
    std::string id;
    std::string kind;
    std::string provider;
    ComponentState state = ComponentState::NotChecked;
    std::string diagnostic;
    std::optional<std::chrono::system_clock::time_point> observedAt;
};

struct DesktopSnapshot {
    int schema = 1;
    Edition edition = Edition::Engineering;
    std::string projectId;
    std::string projectTitle;
    std::string projectVersion;
    std::string profileId;
    std::string profileVersion;
    std::string catalogPath;
    std::vector<std::string> dutTypes;
    // A declaration from YAML, not runtime permission to energize outputs.
    bool activeOutputsDeclaredConfirmed = false;
    RuntimeState runtimeState = RuntimeState::Unconfigured;
    bool simulated = false;
    std::string activeRunId;
    std::optional<RunVerdict> lastRunVerdict;
    bool stopAvailable = false;
    std::vector<WorkflowView> workflows;
    std::vector<ComponentView> components;
};

struct ParameterView {
    std::string sourceId;
    std::string key;
    ParameterState state = ParameterState::NoData;
    std::optional<double> value;
    std::string unit;
    std::string diagnostic;
    std::optional<std::chrono::system_clock::time_point> observedAt;
    bool graphAllowed = false;
};

// The caller supplies a single already-observed sample, not a live device.
// An unknown unit, non-finite value, invalid quality or stale timestamp must
// never be converted to a confirmed plotted value.
ParameterView makeParameterView(
    std::string sourceId, std::string key,
    const std::optional<ParameterSample>& sample,
    const std::string& expectedUnit,
    std::chrono::system_clock::time_point now,
    std::chrono::milliseconds maxAge);

// Immutable package composition + read-only projection/command preflight.
// No StationSession ownership, device construction, network/USB access or
// GUI-thread assumptions. Calls into hardware belong to a separate application
// service, which MUST repeat authorization and readiness checks.
class DesktopCore final {
public:
    static DesktopCore load(const std::string& projectFile, DesktopPolicy policy);
    DesktopCore(ProjectDefinition project, StandProfile profile, DesktopPolicy policy);

    const ProjectDefinition& project() const noexcept { return project_; }
    const StandProfile& profile() const noexcept { return profile_; }
    const DesktopPolicy& policy() const noexcept { return policy_; }

    Decision check(const CommandRequest& command, const RuntimeObservation& runtime) const;
    DesktopSnapshot snapshot(const RuntimeObservation& runtime) const;

private:
    ProjectDefinition project_;
    StandProfile profile_;
    DesktopPolicy policy_;
};

} // namespace orbita::stand::desktop
