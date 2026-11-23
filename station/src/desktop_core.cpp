#include "orbita_stand/desktop_core.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace orbita::stand::desktop {

const char* toString(Edition value) noexcept
{
    switch (value) {
    case Edition::Engineering: return "engineering";
    case Edition::Tu: return "tu";
    }
    return "unknown";
}

const char* toString(RuntimeState value) noexcept
{
    switch (value) {
    case RuntimeState::Unconfigured: return "unconfigured";
    case RuntimeState::Idle: return "idle";
    case RuntimeState::Busy: return "busy";
    case RuntimeState::Stopping: return "stopping";
    case RuntimeState::Fault: return "fault";
    }
    return "unknown";
}

const char* toString(ComponentState value) noexcept
{
    switch (value) {
    case ComponentState::Disabled: return "disabled";
    case ComponentState::NotChecked: return "not_checked";
    case ComponentState::Ready: return "ready";
    case ComponentState::Disconnected: return "disconnected";
    case ComponentState::Fault: return "fault";
    case ComponentState::Simulated: return "simulated";
    }
    return "unknown";
}

const char* toString(ParameterState value) noexcept
{
    switch (value) {
    case ParameterState::NoData: return "no_data";
    case ParameterState::Fresh: return "fresh";
    case ParameterState::Stale: return "stale";
    case ParameterState::Disconnected: return "disconnected";
    case ParameterState::Invalid: return "invalid";
    case ParameterState::UnknownUnit: return "unknown_unit";
    case ParameterState::UnitMismatch: return "unit_mismatch";
    }
    return "unknown";
}

const char* toString(CommandKind value) noexcept
{
    switch (value) {
    case CommandKind::StartWorkflow: return "start_workflow";
    case CommandKind::RequestStop: return "request_stop";
    case CommandKind::CreateScenarioDraft: return "create_scenario_draft";
    }
    return "unknown";
}

ParameterView makeParameterView(
    std::string sourceId, std::string key,
    const std::optional<ParameterSample>& sample,
    const std::string& expectedUnit,
    std::chrono::system_clock::time_point now,
    std::chrono::milliseconds maxAge)
{
    ParameterView view;
    view.sourceId = std::move(sourceId);
    view.key = std::move(key);
    if (!sample) return view;

    view.diagnostic = sample->diagnostic;
    if (sample->parameterKey != view.key) {
        view.state = ParameterState::Invalid;
        view.diagnostic = "Parameter key does not match the requested binding";
        return view;
    }
    switch (sample->quality) {
    case SampleQuality::Stale:
        view.state = ParameterState::Stale;
        return view;
    case SampleQuality::Disconnected:
        view.state = ParameterState::Disconnected;
        return view;
    case SampleQuality::Invalid:
        view.state = ParameterState::Invalid;
        return view;
    case SampleQuality::Good:
        break;
    }

    if (!std::isfinite(sample->physicalValue)
        || sample->timestamp == std::chrono::system_clock::time_point{}
        || sample->timestamp > now || maxAge.count() < 0) {
        view.state = ParameterState::Invalid;
        view.diagnostic = "Non-finite value, invalid timestamp or invalid freshness limit";
        return view;
    }
    view.observedAt = sample->timestamp;
    if (now - sample->timestamp > maxAge) {
        view.state = ParameterState::Stale;
        return view;
    }
    view.unit = sample->unit;
    view.value = sample->physicalValue;
    if (expectedUnit.empty()) {
        view.state = ParameterState::UnknownUnit;
        view.diagnostic = "The parameter unit has not been confirmed by the delivery";
    } else if (sample->unit != expectedUnit) {
        view.state = ParameterState::UnitMismatch;
        view.diagnostic = "Observed unit does not match the declared parameter unit";
    } else {
        view.state = ParameterState::Fresh;
        view.graphAllowed = true;
    }
    return view;
}

DesktopCore DesktopCore::load(const std::string& projectFile, DesktopPolicy policy)
{
    auto project = loadProjectPackage(projectFile);
    auto profile = loadStandProfile(project.equipmentProfilePath);
    return DesktopCore(std::move(project), std::move(profile), std::move(policy));
}

DesktopCore::DesktopCore(
    ProjectDefinition project, StandProfile profile, DesktopPolicy policy)
    : project_(std::move(project))
    , profile_(std::move(profile))
    , policy_(std::move(policy))
{
    if (project_.id.empty() || project_.version.empty() || profile_.id.empty())
        throw std::invalid_argument("Desktop core requires a project and stand profile identity");
    if (policy_.edition == Edition::Tu) {
        const auto* selected = findWorkflow(project_, policy_.tuWorkflowId);
        if (!selected || selected->kind != "tu")
            throw std::invalid_argument(
                "TU executable must bind one existing workflow of kind tu");
    }
}

Decision DesktopCore::check(
    const CommandRequest& command, const RuntimeObservation& runtime) const
{
    if (command.kind == CommandKind::RequestStop)
        return runtime.busy ? Decision{true, {}} : Decision{false, "No active run to stop"};

    const auto* workflow = findWorkflow(project_, command.workflowId);
    if (!workflow) return {false, "Unknown workflow id"};
    if (policy_.edition == Edition::Tu && command.workflowId != policy_.tuWorkflowId)
        return {false, "Workflow is not part of this TU executable"};
    if (!workflow->operatorAvailable)
        return {false, workflow->unavailableReason.empty()
            ? "Workflow is disabled by the delivery" : workflow->unavailableReason};
    if (workflow->operatorAction.empty() ||
        !policy_.supportedActions.count(workflow->operatorAction))
        return {false, "No approved application handler for the workflow action"};

    if (command.kind == CommandKind::CreateScenarioDraft) {
        if (policy_.edition != Edition::Engineering || !policy_.allowScenarioEditing)
            return {false, "Scenario editing is not available in this executable"};
        if (runtime.busy) return {false, "The station is performing another operation"};
        if (!workflow->allowDynamicScenario && !workflow->allowScenarioOverrides)
            return {false, "The workflow does not permit scenario editing"};
        return {true, {}};
    }

    if (command.kind != CommandKind::StartWorkflow)
        return {false, "Unsupported desktop command"};
    if (runtime.busy || runtime.stopping)
        return {false, "The station is busy or stopping"};
    if (runtime.fault) return {false, "Station fault requires resolution"};
    if (!runtime.configured) return {false, "Station runtime is not configured"};
    if (!runtime.readinessConfirmed)
        return {false, "Equipment readiness has not been confirmed"};
    if (policy_.edition == Edition::Tu && runtime.simulated
        && !policy_.allowSimulationInTu)
        return {false, "Simulation is not approved for this TU executable"};
    if (workflow->registration.required && !runtime.dutRegistered)
        return {false, "Workflow requires a registered device"};
    if (workflow->environmentMode == "manual_or_controlled"
        && !runtime.environmentConfirmed)
        return {false, "Workflow requires confirmed environmental conditions"};
    return {true, {}};
}

DesktopSnapshot DesktopCore::snapshot(const RuntimeObservation& runtime) const
{
    DesktopSnapshot result;
    result.edition = policy_.edition;
    result.projectId = project_.id;
    result.projectTitle = project_.title;
    result.projectVersion = project_.version;
    result.profileId = profile_.id;
    result.profileVersion = profile_.version;
    result.catalogPath = project_.catalogPath;
    result.dutTypes = project_.dutTypes;
    result.activeOutputsDeclaredConfirmed = profile_.activeOutputsConfirmed;
    result.runtimeState = runtime.fault ? RuntimeState::Fault
        : runtime.stopping ? RuntimeState::Stopping
        : runtime.busy ? RuntimeState::Busy
        : runtime.configured ? RuntimeState::Idle : RuntimeState::Unconfigured;
    result.simulated = runtime.simulated;
    result.activeRunId = runtime.activeRunId;
    result.lastRunVerdict = runtime.lastRunVerdict;
    result.stopAvailable = check({CommandKind::RequestStop, {}}, runtime).allowed;

    for (const auto& workflow : project_.workflows) {
        if (policy_.edition == Edition::Tu && workflow.id != policy_.tuWorkflowId)
            continue;
        const auto run = check({CommandKind::StartWorkflow, workflow.id}, runtime);
        const auto draft = check({CommandKind::CreateScenarioDraft, workflow.id}, runtime);
        WorkflowView item;
        item.id = workflow.id;
        item.title = workflow.title;
        item.kind = workflow.kind;
        item.action = workflow.operatorAction;
        item.launchable = run.allowed;
        item.draftCreationAvailable = draft.allowed;
        item.unavailableReason = run.reason;
        result.workflows.push_back(std::move(item));
    }
    for (const auto& component : profile_.components) {
        ComponentView item;
        item.id = component.id;
        item.kind = component.kind;
        item.provider = component.provider;
        if (!component.enabled) {
            item.state = ComponentState::Disabled;
            item.diagnostic = "Disabled in the station profile";
        } else if (runtime.configured) {
            const auto found = runtime.components.find(component.id);
            if (found != runtime.components.end() && found->second.observedAt) {
                item.state = found->second.state;
                item.diagnostic = found->second.diagnostic;
                item.observedAt = found->second.observedAt;
            }
        }
        result.components.push_back(std::move(item));
    }
    return result;
}

} // namespace orbita::stand::desktop
