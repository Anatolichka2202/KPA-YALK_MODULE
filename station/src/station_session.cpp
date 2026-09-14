#include "orbita_stand/station_session.h"

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
        components_.instantiate(profile, componentKinds);
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
    safeStopAll();
    components_.clear();
    equipment_.clear();
    equipmentDevices_.clear();
    profile_ = {};
    configured_ = false;
}

} // namespace orbita::stand
