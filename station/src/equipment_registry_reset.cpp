#include "orbita_stand/equipment_runtime.h"

#include <set>

namespace orbita::stand {

void EquipmentRegistry::clearPhysical() noexcept
{
    std::set<EquipmentDevice*> stopped;
    for (const auto& [capability, device] : bindings_) {
        (void)capability;
        if (device && stopped.insert(device.get()).second) device->safeStop();
    }
    for (const auto& [resourceId, binding] : resourceBindings_) {
        (void)resourceId;
        if (binding.device && stopped.insert(binding.device.get()).second) {
            binding.device->safeStop();
        }
    }

    bindings_.clear();
    resourceBindings_.clear();
}

} // namespace orbita::stand
