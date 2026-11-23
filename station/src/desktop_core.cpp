#include "orbita_stand/desktop_core.h"
#include "orbita_stand/station_capabilities.h"
#include "orbita_stand/station_session.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
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

namespace {
bool sameProject(const ProjectDefinition& a, const ProjectDefinition& b)
{
    if (a.id != b.id || a.title != b.title || a.version != b.version || a.rootDirectory != b.rootDirectory
        || a.equipmentProfilePath != b.equipmentProfilePath || a.dutTypes != b.dutTypes
        || a.screenPaths != b.screenPaths || a.reportPaths != b.reportPaths || a.scriptPaths != b.scriptPaths
        || a.catalogPath != b.catalogPath || a.workflows.size() != b.workflows.size()) return false;
    for (std::size_t i = 0; i < a.workflows.size(); ++i) {
        const auto& x = a.workflows[i]; const auto& y = b.workflows[i];
        if (x.id != y.id || x.title != y.title || x.kind != y.kind || x.sourcePath != y.sourcePath
            || x.scenarioPath != y.scenarioPath || x.reportPath != y.reportPath || x.environmentPath != y.environmentPath
            || x.environmentId != y.environmentId || x.environmentTitle != y.environmentTitle
            || x.environmentMode != y.environmentMode || x.referenceWorkflow != y.referenceWorkflow
            || x.operatorAction != y.operatorAction || x.unavailableReason != y.unavailableReason
            || x.operatorAvailable != y.operatorAvailable || x.allowDynamicScenario != y.allowDynamicScenario
            || x.allowScenarioOverrides != y.allowScenarioOverrides
            || x.registration.required != y.registration.required || x.registration.attachIfFound != y.registration.attachIfFound)
            return false;
    }
    return true;
}

bool samePolicy(const DesktopPolicy& a, const DesktopPolicy& b)
{
    return a.edition == b.edition && a.supportedActions == b.supportedActions && a.tuWorkflowId == b.tuWorkflowId
        && a.allowScenarioEditing == b.allowScenarioEditing && a.allowSimulationInTu == b.allowSimulationInTu;
}

bool sameProfile(const StandProfile& left, const StandProfile& right)
{
    if (left.id != right.id || left.version != right.version || left.title != right.title
        || left.activeOutputsConfirmed != right.activeOutputsConfirmed || left.routes != right.routes
        || left.connections != right.connections || left.components.size() != right.components.size()
        || left.devices.size() != right.devices.size()) return false;
    for (std::size_t i = 0; i < left.components.size(); ++i) {
        const auto& a = left.components[i]; const auto& b = right.components[i];
        if (a.id != b.id || a.kind != b.kind || a.provider != b.provider || a.enabled != b.enabled
            || a.bindings != b.bindings || a.capabilities != b.capabilities || a.configuration != b.configuration) return false;
    }
    for (std::size_t i = 0; i < left.devices.size(); ++i) {
        const auto& a = left.devices[i]; const auto& b = right.devices[i];
        if (a.id != b.id || a.pluginId != b.pluginId || a.enabled != b.enabled
            || a.bindCapabilities != b.bindCapabilities || a.configuration != b.configuration) return false;
    }
    return true;
}

bool scopeAllows(ApplicationScope scope, Edition edition)
{
    return scope == ApplicationScope::Both
        || (scope == ApplicationScope::Engineering && edition == Edition::Engineering)
        || (scope == ApplicationScope::Tu && edition == Edition::Tu);
}

QString qtext(const std::string& text) { return QString::fromUtf8(text.data(), static_cast<int>(text.size())); }

QJsonObject moduleJson(const ModuleView& module)
{
    QJsonArray features;
    for (const auto& feature : module.features)
        features.append(QJsonObject{{"id", qtext(feature.id)}, {"title", qtext(feature.title)},
            {"availability", toString(feature.state)}, {"reason", qtext(feature.reason)},
            {"connection", toString(feature.connection)}});
    return {{"id", qtext(module.id)}, {"title", qtext(module.title)}, {"category", qtext(module.category)},
        {"implementation", qtext(module.implementation)}, {"availability", toString(module.state)},
        {"reason", qtext(module.reason)}, {"connection", toString(module.connection)}, {"features", features},
        {"features_complete", module.featuresComplete}};
}
}

const char* toString(CapabilityAvailability value) noexcept
{
    switch (value) {
    case CapabilityAvailability::Available: return "available";
    case CapabilityAvailability::Unavailable: return "unavailable";
    case CapabilityAvailability::Forbidden: return "forbidden";
    case CapabilityAvailability::NotRegistered: return "not_registered";
    case CapabilityAvailability::Absent: return "absent";
    }
    return "unavailable";
}

const char* toString(ServiceConnection value) noexcept
{
    switch (value) {
    case ServiceConnection::NotApplicable: return "not_applicable";
    case ServiceConnection::Unknown: return "unknown";
    case ServiceConnection::Connected: return "connected";
    case ServiceConnection::Disconnected: return "disconnected";
    }
    return "unknown";
}

StationCapabilityRegistry::StationCapabilityRegistry(const DesktopCore& core, const ScenarioEngine& engine)
    : core_(&core), coreSnapshot_(core), engine_(&engine), projectId_(core.project().id), projectVersion_(core.project().version),
      profileId_(core.profile().id), profileVersion_(core.profile().version)
{
    if (projectId_.empty() || projectVersion_.empty() || profileId_.empty() || profileVersion_.empty())
        throw std::invalid_argument("Реестру нужны идентификатор и версия проекта");
}

void StationCapabilityRegistry::bindSession(StationSession& session)
{
    auto guard = session.beginActivity();
    if (!guard) throw std::runtime_error("Сессия занята, привязка невозможна");
    if (session_ && (session_ != &session || sessionGeneration_ != session.compositionGeneration()))
        throw std::logic_error("После изменения сессии требуется новый реестр");
    if (!sameProject(core_->project(), coreSnapshot_.project()) || !sameProfile(core_->profile(), coreSnapshot_.profile())
        || !samePolicy(core_->policy(), coreSnapshot_.policy()) || !session.configured()
        || &session.scenarioEngine() != engine_ || !sameProfile(session.profile(), coreSnapshot_.profile()))
        throw std::invalid_argument("Состав или движок сессии не совпадает с реестром");
    session_ = &session;
    sessionGeneration_ = session.compositionGeneration();
}

void StationCapabilityRegistry::registerModule(ModuleRegistration module)
{
    if (session_) throw std::logic_error("После привязки сессии регистрацию менять нельзя");
    if (module.id.empty() || module.title.empty() || module.category.empty()
        || module.implementation.empty() || !module.observe)
        throw std::invalid_argument("Модуль требует идентификатор, категорию, реализацию и источник состояния");
    std::set<std::string> featureIds;
    for (const auto& feature : module.features)
        if (feature.id.empty() || feature.title.empty() || !feature.observe || !featureIds.insert(feature.id).second)
            throw std::invalid_argument("Некорректная или повторяющаяся функция модуля " + module.id);
    const auto id = module.id;
    if (!modules_.emplace(id, std::move(module)).second)
        throw std::invalid_argument("Повторная регистрация модуля " + id);
}

void StationCapabilityRegistry::markInventoryComplete()
{
    if (session_) throw std::logic_error("После привязки сессии регистрацию менять нельзя");
    inventoryComplete_ = true;
}

void StationCapabilityRegistry::exposeAction(std::string actionId, ApplicationScope scope)
{
    if (session_) throw std::logic_error("После привязки сессии регистрацию менять нельзя");
    const auto registered = engine_->registeredProcedures();
    if (actionId.empty() || std::find(registered.begin(), registered.end(), actionId) == registered.end())
        throw std::invalid_argument("Действие не зарегистрировано в движке: " + actionId);
    if (!actions_.emplace(std::move(actionId), scope).second)
        throw std::invalid_argument("Действие уже включено в состав приложения");
}

ApplicationComposition StationCapabilityRegistry::composeSnapshot(const DesktopCore& core,
    const RuntimeObservation& runtime, CompositionIdentity identity,
    const std::vector<std::string>& requestedModules,
    const std::vector<DiscoveredMethodology>& discovered) const
{
    const auto& engine = *engine_;
    if (&core != core_ || !sameProject(core.project(), coreSnapshot_.project())
        || !sameProfile(core.profile(), coreSnapshot_.profile()) || !samePolicy(core.policy(), coreSnapshot_.policy())
        || core.project().id != projectId_ || core.project().version != projectVersion_
        || core.profile().id != profileId_ || core.profile().version != profileVersion_)
        throw std::invalid_argument("Реестр относится к другому проекту или профилю");
    if (identity.deliveryId.empty() && !identity.deliveryVersion.empty())
        throw std::invalid_argument("Версия поставки задана без идентификатора");
    ApplicationComposition result;
    result.identity = std::move(identity);
    result.inventoryComplete = inventoryComplete_;
    result.desktop = core.snapshot(runtime);
    // Занятость может означать подготовку или ручную команду и не доказывает,
    // что текущую операцию можно остановить через движок сценариев.
    result.desktop.stopAvailable = false;
    result.stopAvailabilityKnown = !runtime.busy;
    for (const auto& [id, registration] : modules_) {
        ModuleView view;
        view.id = id; view.title = registration.title; view.category = registration.category;
        view.implementation = registration.implementation;
        view.featuresComplete = registration.featuresComplete;
        const bool moduleAllowed = scopeAllows(registration.scope, core.policy().edition);
        if (!moduleAllowed) {
            view.state = CapabilityAvailability::Forbidden;
            view.reason = "Модуль не разрешён для этого типа приложения";
            view.connection = ServiceConnection::Unknown;
        } else {
            CapabilityObservation observation;
            try { observation = registration.observe(runtime); }
            catch (const std::exception& error) { observation.reason = error.what(); observation.connection = ServiceConnection::Unknown; }
            catch (...) { observation.reason = "Не удалось определить состояние модуля"; observation.connection = ServiceConnection::Unknown; }
            view.connection = observation.connection;
            view.state = observation.available ? CapabilityAvailability::Available : CapabilityAvailability::Unavailable;
            view.reason = observation.available ? std::string() : (observation.reason.empty()
                ? "Готовность модуля не подтверждена" : observation.reason);
        }
        for (const auto& feature : registration.features) {
            CapabilityView item{feature.id, feature.title, view.state, view.reason, ServiceConnection::Unknown};
            if (!scopeAllows(feature.scope, core.policy().edition)) {
                item.state = CapabilityAvailability::Forbidden;
                item.reason = "Функция не разрешена для этого типа приложения";
                item.connection = ServiceConnection::Unknown;
            } else if (view.state == CapabilityAvailability::Available) {
                CapabilityObservation observation;
                try { observation = feature.observe(runtime); }
                catch (const std::exception& error) { observation.reason = error.what(); observation.connection = ServiceConnection::Unknown; }
                catch (...) { observation.reason = "Не удалось определить состояние функции"; observation.connection = ServiceConnection::Unknown; }
                item.state = observation.available ? CapabilityAvailability::Available : CapabilityAvailability::Unavailable;
                item.connection = observation.connection;
                item.reason = observation.available ? std::string() : (observation.reason.empty()
                    ? "Готовность функции не подтверждена" : observation.reason);
            }
            view.features.push_back(std::move(item));
        }
        result.modules.push_back(std::move(view));
    }
    for (const auto& id : requestedModules) {
        const auto found = std::find_if(result.modules.begin(), result.modules.end(),
            [&](const ModuleView& module) { return module.id == id; });
        if (found != result.modules.end()) result.requestedModules.push_back(*found);
        else {
            ModuleView missing;
            missing.id = id;
            missing.state = inventoryComplete_ ? CapabilityAvailability::Absent : CapabilityAvailability::NotRegistered;
            missing.reason = inventoryComplete_ ? "Модуль отсутствует в подтверждённом составе"
                : "Модуль не зарегистрирован в текущем реестре; полнота состава не подтверждена";
            result.requestedModules.push_back(std::move(missing));
        }
    }
    std::set<std::string> referencedTuActions;
    std::map<std::string, std::string> knownDefinitions;
    bool tuScenarioLoaded = false;
    for (auto& visible : result.desktop.workflows) {
        MethodologyView method;
        method.workflowId = visible.id;
        const auto* workflow = findWorkflow(core.project(), visible.id);
        if (!workflow || workflow->scenarioPath.empty()) {
            method.reason = workflow && workflow->allowDynamicScenario
                ? "Методика выбирается при запуске" : "Фиксированная методика не задана";
        } else try {
            const auto scenario = loadScenarioYaml(workflow->scenarioPath);
            if (core.policy().edition == Edition::Tu) {
                tuScenarioLoaded = true;
                std::function<void(const ScenarioNode&)> collect = [&](const ScenarioNode& node) {
                    if (!node.procedure.empty()) referencedTuActions.insert(node.procedure);
                    for (const auto& child : node.children) collect(child);
                };
                for (const auto& node : scenario.steps) collect(node);
            }
            method.id = scenario.id; method.title = scenario.title; method.version = scenario.version;
            knownDefinitions[visible.id + "\n" + scenario.id + "\n" + scenario.version] = scenarioDefinitionSnapshot(scenario);
            method.published = scenario.publicationState == PublicationState::Published;
            const auto errors = engine.validate(scenario);
            method.valid = errors.empty();
            if (!errors.empty()) method.reason = errors.front();
            else {
                std::set<std::string> denied;
                std::function<void(const ScenarioNode&)> collect = [&](const ScenarioNode& node) {
                    const auto action = actions_.find(node.procedure);
                    if (!node.procedure.empty() && (action == actions_.end() || !scopeAllows(action->second, core.policy().edition)))
                        denied.insert(node.procedure);
                    for (const auto& child : node.children) collect(child);
                };
                for (const auto& node : scenario.steps) collect(node);
                if (!denied.empty()) {
                    method.valid = false;
                    method.reason = "Действие не включено в состав приложения: " + *denied.begin();
                }
            }
            if (method.valid && !method.published) method.reason = "Методика не опубликована";
        } catch (const std::exception& error) { method.reason = error.what(); }
        if (workflow && !workflow->scenarioPath.empty() && (!method.valid || !method.published)) {
            visible.launchable = false;
            visible.unavailableReason = method.reason;
        }
        result.methodologies.push_back(std::move(method));
    }
    if (core.policy().edition == Edition::Tu && !discovered.empty())
        throw std::invalid_argument("Динамические методики не входят в ТУ-приложение");
    for (const auto& entry : discovered) {
        const auto* workflow = findWorkflow(core.project(), entry.workflowId);
        if (!workflow || (!workflow->allowDynamicScenario && !workflow->allowScenarioOverrides))
            throw std::invalid_argument("Методика не относится к настраиваемому workflow: " + entry.workflowId);
        if (entry.scenario.id.empty() || entry.scenario.version.empty())
            throw std::invalid_argument("Обнаруженная методика без идентификатора или версии");
        const auto key = entry.workflowId + "\n" + entry.scenario.id + "\n" + entry.scenario.version;
        const auto definition = scenarioDefinitionSnapshot(entry.scenario);
        const auto existing = knownDefinitions.find(key);
        if (existing != knownDefinitions.end()) {
            if (existing->second != definition)
                throw std::invalid_argument("Разные определения одной версии методики: " + entry.scenario.id);
            continue;
        }
        knownDefinitions.emplace(key, definition);
        MethodologyView method;
        method.workflowId = entry.workflowId;
        method.id = entry.scenario.id; method.title = entry.scenario.title; method.version = entry.scenario.version;
        method.published = entry.scenario.publicationState == PublicationState::Published;
        const auto errors = engine.validate(entry.scenario);
        method.valid = errors.empty();
        if (!errors.empty()) method.reason = errors.front();
        if (method.valid) {
            std::function<void(const ScenarioNode&)> checkActions = [&](const ScenarioNode& node) {
                const auto action = actions_.find(node.procedure);
                if (!node.procedure.empty() && (action == actions_.end() || !scopeAllows(action->second, core.policy().edition))) {
                    method.valid = false; method.reason = "Действие не включено в состав приложения: " + node.procedure;
                }
                for (const auto& child : node.children) checkActions(child);
            };
            for (const auto& node : entry.scenario.steps) checkActions(node);
        }
        if (method.valid && !method.published) method.reason = "Методика не опубликована";
        result.methodologies.push_back(std::move(method));
    }
    const auto registered = engine.registeredProcedures();
    if (core.policy().edition == Edition::Tu && !tuScenarioLoaded)
        result.actionDiagnostic = result.methodologies.empty()
            ? "Фиксированный сценарий ТУ не задан" : result.methodologies.front().reason;
    for (const auto& id : registered) {
        const auto action = actions_.find(id);
        if (action != actions_.end() && scopeAllows(action->second, core.policy().edition)
            && (core.policy().edition != Edition::Tu || referencedTuActions.count(id))) result.actionHandlers.push_back(id);
    }
    return result;
}

ApplicationComposition StationCapabilityRegistry::compose(const DesktopCore& core, const RuntimeObservation& runtime,
    CompositionIdentity identity, const std::vector<std::string>& requestedModules,
    const std::vector<DiscoveredMethodology>& discovered) const
{
    if (session_) throw std::logic_error("Для привязанной сессии используйте согласованный снимок");
    return composeSnapshot(core, runtime, std::move(identity), requestedModules, discovered);
}

ApplicationComposition StationCapabilityRegistry::compose(const DesktopCore& core, StationSession& session,
    CompositionIdentity identity, const std::vector<std::string>& requestedModules,
    const std::vector<DiscoveredMethodology>& discovered) const
{
    bool expected = false;
    if (!snapshotting_.compare_exchange_strong(expected, true))
        throw std::runtime_error("Снимок состава уже строится");
    struct SnapshotGuard {
        std::atomic_bool& flag;
        ~SnapshotGuard() { flag.store(false); }
    } snapshotGuard{snapshotting_};
    auto guard = session.beginActivity();
    if (!guard) throw std::runtime_error("Сессия занята, согласованный снимок недоступен");
    if (session_ != &session || sessionGeneration_ != session.compositionGeneration()
        || !session.configured() || &session.scenarioEngine() != engine_ || !sameProfile(session.profile(), core.profile()))
        throw std::invalid_argument("Сессия изменилась или не привязана к реестру");
    RuntimeObservation observation;
    observation.configured = true;
    auto result = composeSnapshot(core, observation, std::move(identity), requestedModules, discovered);
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        lastSessionSnapshot_ = result;
    }
    return result;
}

ApplicationComposition StationCapabilityRegistry::composeDuringActivity(const DesktopCore& core, const StationSession& session) const
{
    if (&core != core_ || !sameProject(core.project(), coreSnapshot_.project())
        || !sameProfile(core.profile(), coreSnapshot_.profile()) || !samePolicy(core.policy(), coreSnapshot_.policy())
        || session_ != &session || sessionGeneration_ != session.compositionGeneration()
        || !session.busy() || snapshotting_.load())
        throw std::runtime_error("Нет актуального снимка для занятой сессии");
    std::unique_lock<std::mutex> lock(snapshotMutex_);
    if (!lastSessionSnapshot_ || snapshotting_.load() || !session.busy()
        || sessionGeneration_ != session.compositionGeneration())
        throw std::runtime_error("Нет актуального снимка для занятой сессии");
    auto result = *lastSessionSnapshot_;
    lock.unlock();
    result.staleDuringActivity = true;
    result.stopAvailabilityKnown = false;
    result.desktop.runtimeState = RuntimeState::Busy;
    // Общий резерв занят также при подготовке и ручных командах; это не
    // подтверждает наличие останавливаемого запуска сценария.
    result.desktop.stopAvailable = false;
    for (auto& workflow : result.desktop.workflows) {
        workflow.launchable = false;
        workflow.draftCreationAvailable = false;
        workflow.unavailableReason = "Станция занята";
    }
    const auto markStale = [](ModuleView& module) {
        module.connection = ServiceConnection::Unknown;
        if (module.state == CapabilityAvailability::Available) {
            module.state = CapabilityAvailability::Unavailable;
            module.reason = "Состояние не обновлялось во время работы станции";
        }
        for (auto& feature : module.features) {
            feature.connection = ServiceConnection::Unknown;
            if (feature.state == CapabilityAvailability::Available) {
                feature.state = CapabilityAvailability::Unavailable;
                feature.reason = "Состояние не обновлялось во время работы станции";
            }
        }
    };
    for (auto& module : result.modules) markStale(module);
    for (auto& module : result.requestedModules) markStale(module);
    return result;
}

QJsonObject compositionToJson(const ApplicationComposition& composition)
{
    QJsonArray modules, requested, actions, workflows, components, methodologies;
    for (const auto& item : composition.modules) modules.append(moduleJson(item));
    for (const auto& item : composition.requestedModules) requested.append(moduleJson(item));
    for (const auto& action : composition.actionHandlers) actions.append(qtext(action));
    for (const auto& method : composition.methodologies)
        methodologies.append(QJsonObject{{"workflow_id", qtext(method.workflowId)}, {"id", qtext(method.id)},
            {"title", qtext(method.title)}, {"version", qtext(method.version)}, {"definition_valid", method.valid},
            {"published", method.published}, {"reason", qtext(method.reason)}});
    for (const auto& workflow : composition.desktop.workflows)
        workflows.append(QJsonObject{{"id", qtext(workflow.id)}, {"title", qtext(workflow.title)},
            {"kind", qtext(workflow.kind)}, {"action", qtext(workflow.action)},
            {"preflight_launchable", workflow.launchable}, {"reason", qtext(workflow.unavailableReason)},
            {"policy_allows_draft", workflow.draftCreationAvailable}});
    for (const auto& component : composition.desktop.components)
        components.append(QJsonObject{{"id", qtext(component.id)}, {"kind", qtext(component.kind)},
            {"provider", qtext(component.provider)}, {"state", toString(component.state)},
            {"reason", qtext(component.diagnostic)}});
    const auto& desktop = composition.desktop;
    return {{"schema", composition.schema},
        {"station_id", composition.identity.stationId.empty() ? QJsonValue(QJsonValue::Null) : QJsonValue(qtext(composition.identity.stationId))},
        {"application_type", toString(desktop.edition)},
        {"delivery", composition.identity.deliveryId.empty() ? QJsonValue(QJsonValue::Null) : QJsonValue(QJsonObject{
            {"id", qtext(composition.identity.deliveryId)},
            {"version", composition.identity.deliveryVersion.empty() ? QJsonValue(QJsonValue::Null) : QJsonValue(qtext(composition.identity.deliveryVersion))}})},
        {"project", QJsonObject{{"id", qtext(desktop.projectId)}, {"title", qtext(desktop.projectTitle)},
            {"version", qtext(desktop.projectVersion)}}},
        {"profile", QJsonObject{{"id", qtext(desktop.profileId)}, {"version", qtext(desktop.profileVersion)}}},
        {"runtime_state", toString(desktop.runtimeState)}, {"stop_available", composition.stopAvailabilityKnown ? QJsonValue(desktop.stopAvailable) : QJsonValue(QJsonValue::Null)}, {"inventory_complete", composition.inventoryComplete}, {"stale_during_activity", composition.staleDuringActivity}, {"modules", modules}, {"requested_modules", requested},
        {"registered_action_handlers", actions}, {"action_diagnostic", qtext(composition.actionDiagnostic)},
        {"workflows", workflows}, {"methodologies", methodologies}, {"configured_components", components},
        {"authorization", "description_only_check_application_service_before_command"}};
}

} // namespace orbita::stand::desktop
