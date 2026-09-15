#pragma once

#include "orbita_stand/component_runtime.h"
#include "orbita_stand/config.h"
#include "orbita_stand/equipment_runtime.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace orbita::stand {

// Equipment instances may be created immediately from the profile or deferred
// until an application readiness/commissioning flow selects and probes the
// concrete devices it actually needs. Deferred mode is important for stations
// where device construction claims ports or where power/boot ordering matters.
enum class EquipmentInstantiation {
    Immediate,
    Deferred,
};

// Application-neutral composition/lifecycle root for one configured station.
// UI code should depend on this object instead of owning plugin manager,
// equipment registry, devices and component runtime independently.
//
// Component kinds remain extensible: the application/delivery registers the
// factories it needs (sample_source, execution_runtime, future SSH/Python/Lua,
// ...), then configure() instantiates the selected kinds from the delivery
// profile alongside equipment plugins.
class StationSession final {
public:
    StationSession() = default;
    ~StationSession() { clear(); }

    StationSession(const StationSession&) = delete;
    StationSession& operator=(const StationSession&) = delete;

    ComponentRuntime& components() noexcept { return components_; }
    const ComponentRuntime& components() const noexcept { return components_; }

    EquipmentRegistry& equipment() noexcept { return equipment_; }
    const EquipmentRegistry& equipment() const noexcept { return equipment_; }

    EquipmentPluginManager& equipmentPlugins() noexcept { return equipmentPlugins_; }
    const EquipmentPluginManager& equipmentPlugins() const noexcept { return equipmentPlugins_; }

    // Mutable views are intentionally narrow compatibility hooks for the staged
    // desktop migration. Ownership still remains here; callers must not replace
    // the referenced objects. Once readiness lives outside MainWindow these
    // compatibility views can become const-only again.
    StandProfile& profile() noexcept { return profile_; }
    const StandProfile& profile() const noexcept { return profile_; }

    std::vector<std::shared_ptr<EquipmentDevice>>& equipmentDevices() noexcept
    {
        return equipmentDevices_;
    }
    const std::vector<std::shared_ptr<EquipmentDevice>>& equipmentDevices() const noexcept
    {
        return equipmentDevices_;
    }

    bool configured() const noexcept { return configured_; }

    // `componentKinds` intentionally excludes equipment while the Equipment
    // Plugin ABI is still on its compatibility runtime. Empty means "all
    // registered/non-equipment kinds in the profile".
    //
    // Immediate preserves the original StationSession behaviour. Deferred
    // loads plugin providers and non-equipment components but leaves physical
    // equipment uninstantiated so a readiness flow can create only the devices
    // required by the selected scenario and in the required power-up order.
    void configure(
        StandProfile profile,
        const std::string& pluginDirectory,
        const std::set<std::string>& componentKinds,
        EquipmentInstantiation equipmentInstantiation = EquipmentInstantiation::Immediate);

    // Deferred readiness primitive. Construction is driven by the canonical
    // kind=equipment ComponentProfile and augments provider configuration with
    // profile-level safety/routes exactly as immediate composition does. The
    // instance is retained by the session but is not exported into routing until
    // bindEquipmentComponent() is called after the delivery has probed it.
    std::shared_ptr<EquipmentDevice> createEquipmentComponent(
        const std::string& componentId);

    // Publish a successfully probed deferred device under its concrete id and
    // canonical delivery role aliases. Default capability routing is optional so
    // deliveries can keep active operations unavailable while still exporting
    // explicit resource routing for capabilities they have approved.
    void bindEquipmentComponent(
        const std::string& componentId,
        const std::shared_ptr<EquipmentDevice>& device,
        bool bindDefaultCapabilities = true);

    // Used by deferred readiness flows after they create/probe a device through
    // equipmentPlugins(). The registry owns devices that are bound, while this
    // retention list also keeps successfully created but intentionally unbound
    // devices alive until the next equipment reset/session clear.
    void retainEquipmentDevice(std::shared_ptr<EquipmentDevice> device);

    // Re-run physical readiness without destroying application-level built-in
    // scenario services already installed in EquipmentRegistry.
    void clearPhysicalEquipment() noexcept;
    void clearEquipment() noexcept;

    void safeStopAll() noexcept;
    void clear() noexcept;

private:
    StandProfile profile_;
    EquipmentPluginManager equipmentPlugins_;
    EquipmentRegistry equipment_;
    std::vector<std::shared_ptr<EquipmentDevice>> equipmentDevices_;
    ComponentRuntime components_;
    bool configured_ = false;
};

} // namespace orbita::stand
