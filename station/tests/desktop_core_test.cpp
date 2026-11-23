#include "orbita_stand/desktop_core.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace orbita::stand;
using namespace orbita::stand::desktop;

namespace {

void require(bool condition, const std::string& explanation)
{
    if (!condition) throw std::runtime_error(explanation);
}

void testReferencePackage(const std::filesystem::path& root)
{
    DesktopPolicy policy;
    policy.supportedActions = {"station.generic"};
    policy.allowScenarioEditing = true;

    const auto core = DesktopCore::load(
        (root / "projects/reference_device/project.yaml").string(), policy);
    const auto empty = core.snapshot({});
    require(empty.schema == 1 && empty.projectId == "reference_device"
            && empty.profileId == "reference-simulator" && !empty.activeOutputsDeclaredConfirmed,
        "Incorrect independent package identity or safe default");
    require(empty.runtimeState == RuntimeState::Unconfigured && !empty.simulated
            && !empty.lastRunVerdict && empty.activeRunId.empty(),
        "A package descriptor fabricated a live station or a result");
    require(empty.components.size() == 1
            && empty.components.front().state == ComponentState::NotChecked
            && !empty.components.front().observedAt,
        "Loading the package pretended to probe a parameter source");
    require(empty.workflows.size() == 1 && !empty.workflows.front().launchable
            && empty.workflows.front().draftCreationAvailable,
        "Unconfigured runtime must not launch but may prepare an engineering draft");
    require(!core.check({CommandKind::StartWorkflow, "generic"}, {}).allowed,
        "An unconfigured station is executable");
    require(!core.check({CommandKind::RequestStop, {}}, {}).allowed,
        "A stop command was enabled with no operation");

    RuntimeObservation runtime;
    runtime.configured = true;
    runtime.readinessConfirmed = true;
    runtime.simulated = true;
    require(core.check({CommandKind::StartWorkflow, "generic"}, runtime).allowed,
        "An expressly ready engineering simulator was not permitted");
    require(!core.check({CommandKind::StartWorkflow, "absent"}, runtime).allowed,
        "A non-existent workflow was accepted");

    runtime.components["ref-sim"] = {
        ComponentState::Simulated, "Simulator source confirmed",
        std::chrono::system_clock::now()
    };
    const auto confirmed = core.snapshot(runtime);
    require(confirmed.simulated
            && confirmed.components.front().state == ComponentState::Simulated
            && confirmed.components.front().observedAt
            && confirmed.workflows.front().launchable,
        "Explicit simulator evidence was not projected");
    runtime.configured = false;
    require(core.snapshot(runtime).components.front().state == ComponentState::NotChecked,
        "Stale component readiness leaked into an unconfigured session");

    runtime.configured = true;
    runtime.busy = true;
    require(!core.check({CommandKind::StartWorkflow, "generic"}, runtime).allowed
            && !core.check({CommandKind::CreateScenarioDraft, "generic"}, runtime).allowed
            && core.check({CommandKind::RequestStop, {}}, runtime).allowed,
        "A busy station permitted a parallel operation or blocked a stop");
    runtime.fault = true;
    require(core.snapshot(runtime).runtimeState == RuntimeState::Fault
            && core.check({CommandKind::RequestStop, {}}, runtime).allowed,
        "Station fault suppressed the stop path");
    runtime.busy = false;
    require(!core.check({CommandKind::StartWorkflow, "generic"}, runtime).allowed,
        "Faulted station permitted a run");
}

void testTuPolicy(const std::filesystem::path& root)
{
    DesktopPolicy policy;
    policy.edition = Edition::Tu;
    policy.tuWorkflowId = "tu_normal";
    policy.supportedActions = {"ktma.tu"};
    policy.allowScenarioEditing = true; // A TU executable must still deny it.

    const auto core = DesktopCore::load(
        (root / "projects/ktma/project.yaml").string(), policy);
    auto runtime = RuntimeObservation{};
    runtime.configured = true;
    runtime.readinessConfirmed = true;
    runtime.simulated = true;
    auto snapshot = core.snapshot(runtime);
    require(snapshot.edition == Edition::Tu && snapshot.workflows.size() == 1
            && snapshot.workflows.front().id == "tu_normal",
        "TU build exposed engineering or other production workflows");
    require(!core.check({CommandKind::StartWorkflow, "tu_normal"}, runtime).allowed,
        "TU build accepted simulation by default");
    require(!core.check({CommandKind::CreateScenarioDraft, "tu_normal"}, runtime).allowed
            && !core.check({CommandKind::StartWorkflow, "free"}, runtime).allowed,
        "TU build exposed a free workspace or scenario editing");
    runtime.simulated = false;
    require(core.check({CommandKind::StartWorkflow, "tu_normal"}, runtime).allowed,
        "Configured non-simulated TU workflow unexpectedly denied");
    runtime.readinessConfirmed = false;
    require(!core.check({CommandKind::StartWorkflow, "tu_normal"}, runtime).allowed,
        "TU build bypassed equipment readiness");

    bool invalidTu = false;
    try {
        policy.tuWorkflowId = "unknown";
        (void)DesktopCore::load(
            (root / "projects/ktma/project.yaml").string(), policy);
    } catch (const std::invalid_argument&) {
        invalidTu = true;
    }
    require(invalidTu, "TU build accepted an unbound workflow");
    invalidTu = false;
    try {
        policy.tuWorkflowId = "free";
        policy.supportedActions = {"station.free"};
        (void)DesktopCore::load(
            (root / "projects/ktma/project.yaml").string(), policy);
    } catch (const std::invalid_argument&) {
        invalidTu = true;
    }
    require(invalidTu, "TU executable accepted a free engineering workflow");
}

void testExplicitHandlerAllowlist(const std::filesystem::path& root)
{
    DesktopPolicy policy;
    policy.supportedActions = {"ktma.tu"}; // Not the reference project's action.
    const auto core = DesktopCore::load(
        (root / "projects/reference_device/project.yaml").string(), policy);
    RuntimeObservation runtime;
    runtime.configured = true;
    runtime.readinessConfirmed = true;
    require(!core.check({CommandKind::StartWorkflow, "generic"}, runtime).allowed,
        "Arbitrary operator_action token was accepted without a host handler");
    require(!core.snapshot(runtime).workflows.front().unavailableReason.empty(),
        "UI did not receive a reason for an unavailable workflow");
}

void testParameterProjection()
{
    const auto now = std::chrono::system_clock::now();
    ParameterSample sample;
    sample.parameterKey = "voltage";
    sample.timestamp = now - std::chrono::milliseconds(100);
    sample.quality = SampleQuality::Good;
    sample.unit = "V";
    sample.physicalValue = 0.0;

    auto projected = makeParameterView("device", "voltage", sample, "V", now,
                                      std::chrono::seconds(1));
    require(projected.state == ParameterState::Fresh && projected.graphAllowed
            && projected.value && *projected.value == 0.0 && projected.observedAt,
        "A real zero reading was confused with missing data");

    projected = makeParameterView("device", "voltage", {}, "V", now,
                                  std::chrono::seconds(1));
    require(projected.state == ParameterState::NoData && !projected.value
            && !projected.graphAllowed, "Missing data became a zero reading");

    projected = makeParameterView("device", "voltage", sample, "", now,
                                  std::chrono::seconds(1));
    require(projected.state == ParameterState::UnknownUnit
            && projected.value && !projected.graphAllowed,
        "An unconfirmed plotting unit was accepted");
    projected = makeParameterView("device", "voltage", sample, "mV", now,
                                  std::chrono::seconds(1));
    require(projected.state == ParameterState::UnitMismatch
            && !projected.graphAllowed, "Mismatched units were plotted");

    sample.timestamp = now - std::chrono::seconds(20);
    projected = makeParameterView("device", "voltage", sample, "V", now,
                                  std::chrono::seconds(1));
    require(projected.state == ParameterState::Stale && !projected.value
            && !projected.graphAllowed,
        "A stale measurement appeared as a fresh value");
    sample.timestamp = now;
    sample.physicalValue = std::numeric_limits<double>::quiet_NaN();
    projected = makeParameterView("device", "voltage", sample, "V", now,
                                  std::chrono::seconds(1));
    require(projected.state == ParameterState::Invalid
            && !projected.value && !projected.graphAllowed,
        "Non-finite measurement became a valid plot point");

    sample.physicalValue = 12.0;
    sample.quality = SampleQuality::Disconnected;
    projected = makeParameterView("device", "voltage", sample, "V", now,
                                  std::chrono::seconds(1));
    require(projected.state == ParameterState::Disconnected
            && !projected.value && !projected.graphAllowed,
        "Disconnected source appeared as an active measurement");
}

} // namespace

int main()
{
    try {
        const auto source = std::filesystem::path(ORBITA_SOURCE_DIR);
        testReferencePackage(source);
        testTuPolicy(source);
        testExplicitHandlerAllowlist(source);
        testParameterProjection();
        std::cout << "Desktop core contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
