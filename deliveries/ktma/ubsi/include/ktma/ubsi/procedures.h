#pragma once

#include "orbita_stand/scenario.h"

namespace orbita::stand {

// KTMA/UBSI scenario procedures are implemented by the delivery package.
// The historical orbita::stand namespace is retained temporarily for source
// compatibility; new includes must use this delivery-owned header path.
void registerUbsiProcedures(ScenarioEngine& engine);

} // namespace orbita::stand
