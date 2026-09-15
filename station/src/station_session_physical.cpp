#include "orbita_stand/station_session.h"

namespace orbita::stand {

void StationSession::clearPhysicalEquipment() noexcept
{
    equipment_.clearPhysical();
    equipmentDevices_.clear();
}

} // namespace orbita::stand
