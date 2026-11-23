#include "orbita_stand/desktop_core.h"
#include "orbita_stand/station_capabilities.h"
#include "orbita_stand/parameter_procedures.h"
#include "orbita_stand/parameter_repository.h"
#include "orbita_stand/station_session.h"

#include <QJsonArray>
#include <QTemporaryDir>
#include <QFile>

#include <chrono>
#include <atomic>
#include <thread>
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

void testCapabilityComposition(const std::filesystem::path& root)
{
    DesktopPolicy policy;
    policy.supportedActions = {"station.generic"};
    policy.allowScenarioEditing = true;
    const auto reference = DesktopCore::load((root / "projects/reference_device/project.yaml").string(), policy);
    ParameterRepository parameters;
    ScenarioEngine engine;
    registerTimingProcedures(engine);
    registerParameterProcedures(engine, parameters);
    StationCapabilityRegistry registry(reference, engine);
    registry.exposeAction("parameter.measure", ApplicationScope::Engineering);
    registry.exposeAction("station.delay", ApplicationScope::Engineering);
    const auto available = [](const RuntimeObservation&) { return CapabilityObservation{true, {}, ServiceConnection::NotApplicable}; };
    bool engineeringProbed = false;
    registry.registerModule({"editor", "Редактор", "scenarios", "test.editor", ApplicationScope::Engineering,
        {{"edit", "Редактирование", ApplicationScope::Engineering, available}}, true,
        [&](const RuntimeObservation&) { engineeringProbed = true; return CapabilityObservation{true, {}, ServiceConnection::NotApplicable}; }});
    registry.registerModule({"storage", "Хранилище", "local_storage", "test.storage", ApplicationScope::Both,
        {{"read", "Просмотр", ApplicationScope::Both, available},
         {"write", "Запись", ApplicationScope::Engineering, [](const RuntimeObservation&) { return CapabilityObservation{false, "нет БД", ServiceConnection::Disconnected}; }}},
        true, available});
    StationSession session;
    registerTimingProcedures(session.scenarioEngine());
    registerParameterProcedures(session.scenarioEngine(), session.parameters());
    QTemporaryDir plugins;
    require(plugins.isValid(), "temporary plugin directory unavailable");
    session.configure(reference.profile(), plugins.path().toStdString(), {"test-unused-kind"}, EquipmentInstantiation::Deferred);
    StationCapabilityRegistry sessionRegistry(reference, session.scenarioEngine());
    sessionRegistry.exposeAction("parameter.measure", ApplicationScope::Engineering);
    std::atomic_bool holdObservation{false}, observerEntered{false};
    const auto whenIdle = [&](const RuntimeObservation& observation) {
        if (holdObservation.load()) {
            observerEntered.store(true);
            while (holdObservation.load()) std::this_thread::yield();
        }
        return CapabilityObservation{!observation.busy, observation.busy ? "занято" : "", ServiceConnection::NotApplicable};
    };
    sessionRegistry.registerModule({"session-observer", "Наблюдатель", "equipment", "test.observer", ApplicationScope::Both,
        {{"inspect", "Состояние", ApplicationScope::Both, whenIdle}}, true, whenIdle});
    bool unboundRejected = false;
    try { (void)sessionRegistry.compose(reference, session, {}); }
    catch (const std::invalid_argument&) { unboundRejected = true; }
    require(unboundRejected, "unbound session exposed action handlers");
    sessionRegistry.bindSession(session);
    const auto provider = session.profile().components[0].provider;
    session.profile().components[0].provider = "different-provider";
    bool changedProfileRejected = false;
    try { (void)sessionRegistry.compose(reference, session, {}); }
    catch (const std::invalid_argument&) { changedProfileRejected = true; }
    require(changedProfileRejected, "modified profile with same identity was accepted");
    session.profile().components[0].provider = provider;
    const auto sessionView = sessionRegistry.compose(reference, session, {"session-station", {}, {}});
    require(sessionView.desktop.runtimeState == RuntimeState::Idle && !sessionView.desktop.workflows[0].launchable
        && sessionView.desktop.components[0].state == ComponentState::NotChecked
        && sessionView.modules[0].state == CapabilityAvailability::Available,
        "configured session fabricated equipment readiness");
    holdObservation.store(true);
    std::exception_ptr snapshotError;
    std::thread snapshotWorker([&] {
        try { (void)sessionRegistry.compose(reference, session, {}); }
        catch (...) { snapshotError = std::current_exception(); }
    });
    const auto waitUntil = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!observerEntered.load() && std::chrono::steady_clock::now() < waitUntil) std::this_thread::yield();
    bool concurrentRejected = false;
    if (observerEntered.load()) {
        try { (void)sessionRegistry.composeDuringActivity(reference, session); }
        catch (const std::runtime_error&) { concurrentRejected = true; }
    }
    holdObservation.store(false);
    snapshotWorker.join();
    require(observerEntered.load() && concurrentRejected && !snapshotError,
        "concurrent reader treated the snapshot reservation as an active run");
    auto activity = session.beginActivity();
    bool busyRejected = false;
    try { (void)sessionRegistry.compose(reference, session, {}); }
    catch (const std::runtime_error&) { busyRejected = true; }
    const auto busyView = sessionRegistry.composeDuringActivity(reference, session);
    require(busyRejected && busyView.staleDuringActivity && busyView.desktop.runtimeState == RuntimeState::Busy
        && !busyView.desktop.workflows[0].launchable && !busyView.stopAvailabilityKnown && !busyView.desktop.stopAvailable
        && busyView.modules[0].state == CapabilityAvailability::Unavailable
        && busyView.modules[0].connection == ServiceConnection::Unknown,
        "busy session did not return a safe cached composition");
    activity.reset();
    auto dynamicScenario = loadScenarioYaml((root / "projects/reference_device/scenarios/reference_voltage.yaml").string());
    require(sessionRegistry.compose(reference, session, {}, {}, {{"generic", dynamicScenario}}).methodologies.size() == 1,
        "fixed methodology was duplicated by discovery");
    auto conflicting = dynamicScenario;
    conflicting.title = "Конфликт той же версии";
    bool conflictRejected = false;
    try { (void)sessionRegistry.compose(reference, session, {}, {}, {{"generic", conflicting}}); }
    catch (const std::invalid_argument&) { conflictRejected = true; }
    require(conflictRejected, "conflicting definition of the same version was accepted");
    dynamicScenario.id = "reference.user-copy";
    const auto dynamicView = sessionRegistry.compose(reference, session, {}, {}, {{"generic", dynamicScenario}});
    require(dynamicView.methodologies.size() == 2 && dynamicView.methodologies[1].id == "reference.user-copy"
        && dynamicView.methodologies[1].valid,
        "discovered user methodology was not projected from the session engine");
    bool rejectedUnknownWorkflow = false;
    try { (void)sessionRegistry.compose(reference, session, {}, {}, {{"unknown", dynamicScenario}}); }
    catch (const std::invalid_argument&) { rejectedUnknownWorkflow = true; }
    require(rejectedUnknownWorkflow, "discovered method attached to unknown workflow");
    session.clear();
    bool staleSessionRejected = false;
    try { (void)sessionRegistry.compose(reference, session, {}); }
    catch (const std::invalid_argument&) { staleSessionRejected = true; }
    require(staleSessionRejected, "cleared session reused stale action bindings");
    session.resetScenarioEngine();
    bool rawBypassRejected = false;
    try { (void)sessionRegistry.compose(reference, RuntimeObservation{}, {}); }
    catch (const std::logic_error&) { rawBypassRejected = true; }
    require(rawBypassRejected, "raw snapshot bypassed stale session protection");
    session.configure(reference.profile(), plugins.path().toStdString(), {"test-unused-kind"}, EquipmentInstantiation::Deferred);
    bool rebindRejected = false;
    try { sessionRegistry.bindSession(session); }
    catch (const std::logic_error&) { rebindRejected = true; }
    require(rebindRejected, "old module registrations survived session reconfiguration");

    auto incomplete = registry.compose(reference, {}, {"station-test", {}, {}}, {"enterprise"});
    require(incomplete.requestedModules[0].state == CapabilityAvailability::NotRegistered,
        "incomplete inventory falsely reported an absent module");
    registry.markInventoryComplete();
    auto view = registry.compose(reference, {}, {"station-test", {}, {}}, {"enterprise", "storage"});
    require(view.identity.stationId == "station-test" && view.desktop.projectId == "reference_device"
        && view.desktop.components.size() == 1 && view.desktop.components[0].id == "ref-sim"
        && view.desktop.runtimeState == RuntimeState::Unconfigured,
        "composition borrowed KTMA data or invented station readiness");
    require(view.requestedModules.size() == 2 && view.requestedModules[0].state == CapabilityAvailability::Absent
        && view.requestedModules[1].state == CapabilityAvailability::Available
        && view.requestedModules[1].features[1].state == CapabilityAvailability::Unavailable
        && view.requestedModules[1].features[1].connection == ServiceConnection::Disconnected,
        "absent and unavailable module/function states were confused");
    require(view.methodologies.size() == 1 && view.methodologies[0].valid && view.methodologies[0].published
        && view.methodologies[0].id == "reference.simulator.voltage"
        && !view.desktop.workflows[0].launchable,
        "reference methodology or unconfigured readiness was misreported");
    const auto json = compositionToJson(view);
    require(json.value("station_id").toString() == QStringLiteral("station-test")
        && json.value("delivery").isNull()
        && json.value("project").toObject().value("id").toString() == QStringLiteral("reference_device")
        && json.value("inventory_complete").toBool()
        && json.value("modules").toArray().size() == 2
        && json.value("requested_modules").toArray().at(0).toObject().value("availability").toString() == "absent",
        "JSON does not preserve typed module states");
    RuntimeObservation busyRaw;
    busyRaw.configured = true; busyRaw.busy = true;
    require(!registry.compose(reference, busyRaw, {}).stopAvailabilityKnown,
        "a busy non-scenario operation was reported as stoppable");
    DesktopCore mutableCore(reference.project(), reference.profile(), policy);
    StationCapabilityRegistry immutableBinding(mutableCore, engine);
    auto changedPolicy = policy;
    changedPolicy.allowScenarioEditing = false;
    mutableCore = DesktopCore(reference.project(), reference.profile(), changedPolicy);
    bool changedCoreRejected = false;
    try { (void)immutableBinding.compose(mutableCore, RuntimeObservation{}, {}); }
    catch (const std::invalid_argument&) { changedCoreRejected = true; }
    require(changedCoreRejected, "registry accepted an overwritten application policy");
    bool duplicateRejected = false;
    try { registry.registerModule({"storage", "Дубль", "local_storage", "test", ApplicationScope::Both, {}, true, available}); }
    catch (const std::invalid_argument&) { duplicateRejected = true; }
    require(duplicateRejected, "duplicate module registration accepted");

    DesktopPolicy tuPolicy;
    tuPolicy.edition = Edition::Tu; tuPolicy.tuWorkflowId = "tu_normal";
    tuPolicy.supportedActions = {"ktma.tu", "station.generic"};
    tuPolicy.allowScenarioEditing = true;
    const auto ktma = DesktopCore::load((root / "projects/ktma/project.yaml").string(), tuPolicy);
    StationSession switched;
    switched.configure(ktma.profile(), plugins.path().toStdString(), {"test-unused-kind"}, EquipmentInstantiation::Deferred);
    registerTimingProcedures(switched.scenarioEngine());
    switched.configure(reference.profile(), plugins.path().toStdString(), {"test-unused-kind"}, EquipmentInstantiation::Deferred);
    StationCapabilityRegistry switchedRegistry(reference, switched.scenarioEngine());
    switchedRegistry.bindSession(switched);
    const auto switchedView = switchedRegistry.compose(reference, switched, {});
    require(switchedView.actionHandlers.empty() && !switchedView.methodologies[0].valid,
        "actions left in the engine by a previous project leaked into the next composition");
    bool crossProjectRejected = false;
    try { (void)registry.compose(ktma, {}, {}); }
    catch (const std::invalid_argument&) { crossProjectRejected = true; }
    require(crossProjectRejected, "reference registry leaked modules into another project");
    StationCapabilityRegistry ktmaRegistry(ktma, engine);
    ktmaRegistry.registerModule({"editor", "Редактор", "scenarios", "test.editor", ApplicationScope::Engineering,
        {{"edit", "Редактирование", ApplicationScope::Engineering, available}}, true,
        [&](const RuntimeObservation&) { engineeringProbed = true; return CapabilityObservation{true, {}, ServiceConnection::NotApplicable}; }});
    ktmaRegistry.registerModule({"storage", "Хранилище", "local_storage", "test.storage", ApplicationScope::Both,
        {{"read", "Просмотр", ApplicationScope::Both, available},
         {"write", "Запись", ApplicationScope::Engineering, available}}, true, available});
    engineeringProbed = false;
    const auto tu = ktmaRegistry.compose(ktma, {}, {}, {"editor", "storage"});
    require(tu.desktop.projectId == "ktma" && tu.desktop.workflows.size() == 1
        && tu.desktop.workflows[0].id == "tu_normal" && !tu.desktop.workflows[0].draftCreationAvailable
        && tu.requestedModules[0].state == CapabilityAvailability::Forbidden && !engineeringProbed
        && tu.requestedModules[1].features[1].state == CapabilityAvailability::Forbidden,
        "TU composition exposed or probed an engineering-only function");
    require(!tu.methodologies.empty() && !tu.methodologies[0].valid && !tu.desktop.workflows[0].launchable,
        "missing product procedures were reported as an executable TU method");
    auto tuProject = reference.project();
    tuProject.workflows[0].kind = "tu";
    DesktopPolicy isolatedPolicy;
    isolatedPolicy.edition = Edition::Tu;
    isolatedPolicy.tuWorkflowId = tuProject.workflows[0].id;
    isolatedPolicy.supportedActions = {"station.generic"};
    DesktopCore isolatedTu(tuProject, reference.profile(), isolatedPolicy);
    StationCapabilityRegistry isolatedRegistry(isolatedTu, engine);
    isolatedRegistry.exposeAction("parameter.measure", ApplicationScope::Tu);
    const auto tuReference = isolatedRegistry.compose(isolatedTu, {}, {"test-tu", {}, {}});
    require(tuReference.methodologies.size() == 1 && tuReference.methodologies[0].valid
        && tuReference.actionHandlers.size() == 1 && tuReference.actionHandlers[0] == "parameter.measure",
        "explicit TU action scope did not filter registered engineering handlers");
    StationCapabilityRegistry restricted(isolatedTu, engine);
    const auto denied = restricted.compose(isolatedTu, {}, {"test-tu", {}, {}});
    require(!denied.methodologies[0].valid && denied.actionHandlers.empty(),
        "unapproved TU action was exposed or its methodology considered executable");
    QTemporaryDir invalidDirectory;
    require(invalidDirectory.isValid(), "temporary package directory unavailable");
    QFile invalidFile(invalidDirectory.filePath("project.yaml"));
    require(invalidFile.open(QIODevice::WriteOnly), "cannot create malformed package");
    invalidFile.write("schema: 999\nid: broken\n");
    invalidFile.close();
    bool malformedRejected = false;
    try { (void)DesktopCore::load(invalidFile.fileName().toStdString(), policy); }
    catch (const std::exception&) { malformedRejected = true; }
    require(malformedRejected, "malformed package was accepted");
    bool invalidRejected = false;
    try { (void)DesktopCore::load((root / "projects/does-not-exist/project.yaml").string(), policy); }
    catch (const std::exception&) { invalidRejected = true; }
    require(invalidRejected, "invalid package was accepted");
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
        testCapabilityComposition(source);
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
