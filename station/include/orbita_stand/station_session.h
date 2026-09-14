#pragma once

#include "orbita_stand/component_runtime.h"
#include "orbita_stand/config.h"
#include "orbita_stand/equipment_runtime.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace orbita::stand {

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

    const StandProfile& profile() const noexcept { return profile_; }
    const std::vector<std::shared_ptr<EquipmentDevice>>& equipmentDevices() const noexcept
    {
        return equipmentDevices_;
    }

    bool configured() const noexcept { return configured_; }

    // `componentKinds` intentionally excludes equipment while the Equipment
    // Plugin ABI is still on its compatibility runtime. Empty means "all
    // registered/non-equipment kinds in the profile" and is therefore best
    // avoided during the staged migration; callers should normally pass an
    // explicit set such as {"sample_source", "execution_runtime"}.
    void configure(
        StandProfile profile,
        const std::string& pluginDirectory,
        const std::set<std::string>& componentKinds);

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
