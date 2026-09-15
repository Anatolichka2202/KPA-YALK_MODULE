#include "orbita_stand/station_session.h"

#include <algorithm>
#include <set>
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
    equipment_.clear();
    equipmentDevices_.clear();
}

void StationSession::safeStopAll() noexcept
{
    components_.safeStopAll();
    equipment_.safeStopAll();
}

void StationSession::clear() noexcept
{
    components_.clear();
    clearEquipment();
    profile_ = {};
    configured_ = false;
}

} // namespace orbita::stand
