#include "orbita_stand/station_session.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace orbita::stand {

namespace {
// Флаг общий для ручных команд и обоих вариантов запуска. Освобождение
// выполняется и при исключении, чтобы ошибка не блокировала станцию навсегда.
class ActivityGuard {
public:
    explicit ActivityGuard(std::atomic_bool& active) : active_(active)
    {
        bool expected = false;
        acquired_ = active_.compare_exchange_strong(expected, true);
    }
    ~ActivityGuard() { if (acquired_) active_.store(false); }
    explicit operator bool() const noexcept { return acquired_; }
private:
    std::atomic_bool& active_;
    bool acquired_ = false;
};

constexpr auto busyReason = "Станция занята: выполняется другая активная операция";

ScenarioRunResult rejectedRun()
{
    ScenarioRunResult result;
    result.verdict = RunVerdict::Incomplete;
    result.events.push_back({std::chrono::system_clock::now(), {}, "STATION_BUSY",
                             busyReason, RunVerdict::Incomplete});
    return result;
}
}

StationSession::Activity::~Activity()
{
    if (held_) active_->store(false);
}

std::shared_ptr<StationSession::Activity> StationSession::beginActivity()
{
    // Выделяем объект до захвата флага. Если выделение управляющего блока
    // завершится ошибкой, разрушение незахваченного объекта не снимет чужой
    // резерв, полученный другим потоком.
    auto activity = std::shared_ptr<Activity>(new Activity(*this));
    bool expected = false;
    if (!active_->compare_exchange_strong(expected, true)) return {};
    activity->held_ = true;
    return activity;
}

std::shared_ptr<StationSession::Activity> StationSession::claimActivity(
    std::shared_ptr<Activity> activity)
{
    if (!activity) activity = beginActivity();
    if (!activity || activity->owner_ != this || activity->active_ != active_
        || activity->executing_.exchange(true))
        throw std::runtime_error(busyReason);
    return activity;
}

void StationSession::releaseActivity(const std::shared_ptr<Activity>& activity) noexcept
{
    activity->executing_.store(false);
}

StationSession::ActivityUse::ActivityUse(StationSession& session,
    std::shared_ptr<Activity> activity)
    : session_(session), activity_(session.claimActivity(std::move(activity))) {}

StationSession::ActivityUse::~ActivityUse() { session_.releaseActivity(activity_); }

void StationSession::prepare(const std::shared_ptr<Activity>& activity,
    const std::function<void(Preparation&)>& operation)
{
    ActivityUse use(*this, activity);
    Preparation preparation(*this);
    operation(preparation);
}

void StationSession::Preparation::clearPhysicalEquipment()
{
    session_.clearPhysicalEquipmentUnchecked();
}

std::shared_ptr<EquipmentDevice> StationSession::Preparation::createEquipmentComponent(
    const std::string& componentId)
{
    return session_.createEquipmentComponentUnchecked(componentId);
}

void StationSession::Preparation::bindEquipmentComponent(const std::string& componentId,
    const std::shared_ptr<EquipmentDevice>& device, bool bindDefaultCapabilities)
{
    session_.bindEquipmentComponentUnchecked(componentId, device, bindDefaultCapabilities);
}

void StationSession::resetScenarioEngine()
{
    ActivityGuard guard(*active_);
    if (!guard) throw std::runtime_error(busyReason);
    scenarioEngine_ = std::make_unique<ScenarioEngine>();
}

ScenarioRunResult StationSession::runScenario(const ScenarioDefinition& scenario,
    std::string profileVersion, std::string objectSerial, bool allowPartial,
    std::function<void(const RunEvent&)> progressSink, std::shared_ptr<Activity> activity,
    std::function<void(const ScenarioRunResult&)> checkpointSink)
{
    if (!activity) activity = beginActivity();
    if (!activity || (activity->owner_ != this || activity->active_ != active_)
        || activity->executing_.exchange(true))
        return rejectedRun();
    struct ExecutionGuard {
        std::atomic_bool& executing;
        ~ExecutionGuard() { executing.store(false); }
    } guard{activity->executing_};
    return runScenarioWithEvidence(*scenarioEngine_, equipment_, scenario,
        std::move(profileVersion), std::move(objectSerial), allowPartial,
        std::move(progressSink), &leases_, std::move(checkpointSink));
}

ScenarioRunResult StationSession::runWorkflow(const ProjectDefinition& project,
    const std::string& workflowId, std::string profileVersion, std::string objectSerial,
    bool allowPartial, ProjectRunContext context, const ScenarioDefinition* scenarioOverride,
    std::function<void(const RunEvent&)> progressSink, std::shared_ptr<Activity> activity,
    std::function<void(const ScenarioRunResult&)> checkpointSink)
{
    if (!activity) activity = beginActivity();
    if (!activity || (activity->owner_ != this || activity->active_ != active_)
        || activity->executing_.exchange(true))
        return rejectedRun();
    struct ExecutionGuard {
        std::atomic_bool& executing;
        ~ExecutionGuard() { executing.store(false); }
    } guard{activity->executing_};
    return runProjectWorkflow(project, workflowId, *scenarioEngine_, equipment_,
        std::move(profileVersion), std::move(objectSerial), allowPartial,
        std::move(context), scenarioOverride, std::move(progressSink), &leases_);
}

std::string StationSession::invokeManual(const std::string& capability,
    const std::string& operation, const std::map<std::string, std::string>& arguments)
{
    ActivityGuard guard(*active_);
    if (!guard) throw std::runtime_error(busyReason);
    return equipment_.invoke(capability, operation, arguments);
}

void StationSession::configure(
    StandProfile profile,
    const std::string& pluginDirectory,
    const std::set<std::string>& componentKinds,
    EquipmentInstantiation equipmentInstantiation)
{
    ActivityUse activity(*this, {});
    clearUnchecked();
    try {
        equipmentPlugins_.loadDirectory(pluginDirectory);
        if (equipmentInstantiation == EquipmentInstantiation::Immediate) {
            instantiateProfile(profile, equipmentPlugins_, equipment_, equipmentDevices_);
        }

        std::set<std::string> selectedKinds = componentKinds;
        if (selectedKinds.count("equipment")) {
            throw std::invalid_argument(
                "StationSession equipment is owned by Equipment runtime during migration");
        }
        if (selectedKinds.empty()) {
            for (const auto& component : profile.components) {
                if (component.kind != "equipment"
                    && components_.hasKindFactory(component.kind)) {
                    selectedKinds.insert(component.kind);
                }
            }
        }
        if (!selectedKinds.empty()) {
            components_.instantiate(profile, selectedKinds);
        }

        profile_ = std::move(profile);
        configured_ = true;
    } catch (...) {
        clearUnchecked();
        throw;
    }
}

std::shared_ptr<EquipmentDevice> StationSession::createEquipmentComponent(
    const std::string& componentId, std::shared_ptr<Activity> activity)
{
    ActivityUse use(*this, std::move(activity));
    return createEquipmentComponentUnchecked(componentId);
}

std::shared_ptr<EquipmentDevice> StationSession::createEquipmentComponentUnchecked(
    const std::string& componentId)
{
    if (!configured_) {
        throw std::logic_error("StationSession must be configured before equipment readiness");
    }
    const auto* component = findComponentById(profile_, componentId);
    if (!component) {
        throw std::invalid_argument("Equipment component is not declared: " + componentId);
    }
    if (component->kind != "equipment") {
        throw std::invalid_argument("Station component is not equipment: " + componentId);
    }
    if (!component->enabled) {
        throw std::logic_error("Equipment component is disabled: " + componentId);
    }
    if (component->provider.empty()) {
        throw std::invalid_argument("Equipment component provider is empty: " + componentId);
    }

    const auto persistent = component->configuration.find("session_persistent");
    const bool keepForSession = persistent != component->configuration.end()
        && persistent->second == "true";
    if (keepForSession) {
        const auto existing = sessionEquipment_.find(componentId);
        if (existing != sessionEquipment_.end()) return existing->second;
    }

    auto config = component->configuration;
    config["profile.active_outputs_confirmed"] =
        profile_.activeOutputsConfirmed ? "true" : "false";
    for (const auto& [key, value] : profile_.routes) {
        config["route." + key] = value;
    }

    auto device = equipmentPlugins_.createDevice(
        component->provider, component->id, config);
    retainEquipmentDeviceUnchecked(device);
    if (keepForSession) sessionEquipment_.emplace(componentId, device);
    return device;
}

void StationSession::bindEquipmentComponent(
    const std::string& componentId,
    const std::shared_ptr<EquipmentDevice>& device,
    bool bindDefaultCapabilities, std::shared_ptr<Activity> activity)
{
    ActivityUse use(*this, std::move(activity));
    bindEquipmentComponentUnchecked(componentId, device, bindDefaultCapabilities);
}

void StationSession::bindEquipmentComponentUnchecked(const std::string& componentId,
    const std::shared_ptr<EquipmentDevice>& device, bool bindDefaultCapabilities)
{
    if (!configured_) {
        throw std::logic_error("StationSession must be configured before equipment binding");
    }
    if (!device) {
        throw std::invalid_argument("Cannot bind an empty equipment device");
    }
    const auto* component = findComponentById(profile_, componentId);
    if (!component || component->kind != "equipment") {
        throw std::invalid_argument("Equipment component is not declared: " + componentId);
    }
    if (device->instanceId() != component->id) {
        throw std::invalid_argument(
            "Equipment instance does not match component: " + componentId);
    }
    const auto retained = std::find_if(
        equipmentDevices_.begin(), equipmentDevices_.end(),
        [&](const auto& existing) {
            return existing && existing.get() == device.get();
        });
    if (retained == equipmentDevices_.end()) {
        throw std::logic_error(
            "Equipment instance must be retained by StationSession before binding: "
            + componentId);
    }

    const auto& exported = component->capabilities.empty()
        ? component->bindings
        : component->capabilities;
    const std::set<std::string> capabilities(exported.begin(), exported.end());
    if (capabilities.empty()) {
        throw std::invalid_argument(
            "Equipment component exports no capabilities: " + componentId);
    }

    // A delivery role is narrower than a generic provider. Export only the
    // capabilities declared by this concrete component even when the plugin can
    // technically implement more operations.
    equipment_.bindResource(component->id, capabilities, device);
    for (const auto& role : component->bindings) {
        if (!role.empty() && role != component->id) {
            equipment_.bindResource(role, capabilities, device);
        }
    }

    if (!bindDefaultCapabilities) return;
    for (const auto& capability : capabilities) {
        if (!capability.empty()) equipment_.bind(capability, device);
    }
}

void StationSession::retainEquipmentDevice(std::shared_ptr<EquipmentDevice> device,
    std::shared_ptr<Activity> activity)
{
    ActivityUse use(*this, std::move(activity));
    retainEquipmentDeviceUnchecked(std::move(device));
}

void StationSession::retainEquipmentDeviceUnchecked(std::shared_ptr<EquipmentDevice> device)
{
    if (!device) {
        throw std::invalid_argument("Cannot retain an empty equipment device");
    }
    const auto duplicate = std::find_if(
        equipmentDevices_.begin(), equipmentDevices_.end(),
        [&](const auto& existing) {
            return existing && existing->instanceId() == device->instanceId();
        });
    if (duplicate != equipmentDevices_.end()) {
        throw std::invalid_argument(
            "Equipment instance is already retained: " + device->instanceId());
    }
    equipmentDevices_.push_back(std::move(device));
}

void StationSession::clearPhysicalEquipment(std::shared_ptr<Activity> activity)
{
    ActivityUse use(*this, std::move(activity));
    clearPhysicalEquipmentUnchecked();
}

void StationSession::clearPhysicalEquipmentUnchecked() noexcept
{
    equipment_.clearPhysical();
    equipmentDevices_.erase(std::remove_if(equipmentDevices_.begin(), equipmentDevices_.end(),
        [this](const auto& device) {
            return std::none_of(sessionEquipment_.begin(), sessionEquipment_.end(),
                [&](const auto& entry) { return entry.second == device; });
        }), equipmentDevices_.end());
}

void StationSession::clearEquipment(std::shared_ptr<Activity> activity)
{
    ActivityUse use(*this, std::move(activity));
    clearEquipmentUnchecked();
}

void StationSession::clearEquipmentUnchecked() noexcept
{
    equipment_.clear();
    equipmentDevices_.clear();
    sessionEquipment_.clear();
}

void StationSession::safeStopAll(std::shared_ptr<Activity> activity)
{
    ActivityUse use(*this, std::move(activity));
    components_.safeStopAll();
    equipment_.safeStopAll();
}

void StationSession::clear()
{
    ActivityUse use(*this, {});
    clearUnchecked();
}

void StationSession::clearUnchecked() noexcept
{
    components_.clear();
    clearEquipmentUnchecked();
    profile_ = {};
    configured_ = false;
}

} // namespace orbita::stand
