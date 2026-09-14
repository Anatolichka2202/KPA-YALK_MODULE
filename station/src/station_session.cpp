#include "orbita_stand/station_session.h"

#include <stdexcept>
#include <utility>

namespace orbita::stand {

void StationSession::configure(
    StandProfile profile,
    const std::string& pluginDirectory,
    const std::set<std::string>& componentKinds)
{
    clear();
    try {
        equipmentPlugins_.loadDirectory(pluginDirectory);
        instantiateProfile(profile, equipmentPlugins_, equipment_, equipmentDevices_);

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
        components_.instantiate(profile, selectedKinds);

        profile_ = std::move(profile);
        configured_ = true;
    } catch (...) {
        clear();
        throw;
    }
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
    // receive duplicate stop commands merely because a session is reloaded.
    components_.clear();
    equipment_.clear();
    equipmentDevices_.clear();
    profile_ = {};
    configured_ = false;
}

} // namespace orbita::stand
