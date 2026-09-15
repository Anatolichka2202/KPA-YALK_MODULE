#include "orbita_stand/station_session.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace orbita::stand {

void StationSession::configure(
    StandProfile profile,
    const std::string& pluginDirectory,
    const std::set<std::string>& componentKinds,
    EquipmentInstantiation equipmentInstantiation)
{
    clear();
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
            // Empty selection means all non-equipment kinds for which the
            // application/delivery actually registered a factory. Do not pass
            // an empty set to ComponentRuntime because that means "all profile
            // kinds" and would incorrectly attempt to instantiate equipment.
            for (const auto& component : profile.components) {
                if (component.kind != "equipment"
                    && components_.hasKindFactory(component.kind)) {
                    selectedKinds.insert(component.kind);
                }
            }
        }
        // An equipment-only profile in Deferred mode legitimately has no
        // ComponentRuntime work. Passing an empty selection to instantiate()
        // means "all profile kinds", which would incorrectly send equipment
        // through the generic component factory path.
        if (!selectedKinds.empty()) {
            components_.instantiate(profile, selectedKinds);
        }

        profile_ = std::move(profile);
        configured_ = true;
    } catch (...) {
        clear();
        throw;
    }
}

std::shared_ptr<EquipmentDevice> StationSession::createEquipmentComponent(
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

    auto config = component->configuration;
    config["profile.active_outputs_confirmed"] =
        profile_.activeOutputsConfirmed ? "true" : "false";
    for (const auto& [key, value] : profile_.routes) {
        config["route." + key] = value;
    }

    auto device = equipmentPlugins_.createDevice(
        component->provider, component->id, config);
    retainEquipmentDevice(device);
    return device;
}

void StationSession::bindEquipmentComponent(
    const std::string& componentId,
    const std::shared_ptr<EquipmentDevice>& device,
    bool bindDefaultCapabilities)
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

    equipment_.bindResource(component->id, device);
    for (const auto& role : component->bindings) {
        if (!role.empty() && role != component->id) {
            equipment_.bindResource(role, device);
        }
    }

    if (!bindDefaultCapabilities) return;
    const auto& capabilities = component->capabilities.empty()
        ? component->bindings
        : component->capabilities;
    for (const auto& capability : capabilities) {
        if (!capability.empty()) equipment_.bind(capability, device);
    }
}

void StationSession::retainEquipmentDevice(std::shared_ptr<EquipmentDevice> device)
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

void StationSession::clearEquipment() noexcept
{
    // EquipmentRegistry::clear() performs the registry-level best-effort
    // safe-stop. EquipmentDevice destruction also invokes plugin safe_stop,
    // preserving safety for devices retained after a passive probe but not
    // exported into the registry.
    equipment_.clear();
    equipmentDevices_.clear();
}

void StationSession::safeStopAll() noexcept
{
    // Stop non-equipment station components first so producers/runtimes cease
    // activity before active equipment outputs are driven to their safe state.
    components_.safeStopAll();
    equipment_.safeStopAll();
}

void StationSession::clear() noexcept
{
    // Both clear() implementations already perform their own best-effort
    // safe-stop. Do not call safeStopAll() first: active hardware must not
    // receive an extra registry-level stop merely because a session is reloaded.
    components_.clear();
    clearEquipment();
    profile_ = {};
    configured_ = false;
}

} // namespace orbita::stand
