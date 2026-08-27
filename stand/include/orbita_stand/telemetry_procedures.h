#pragma once

#include "orbita_stand/scenario.h"

namespace orbita::stand {

// Общие процедуры контроля потока «Орбита». Они пригодны для БСИ, УБСИ и
// будущих объектов и не содержат адресов конкретного блока.
void registerTelemetryProcedures(ScenarioEngine& engine);

} // namespace orbita::stand
