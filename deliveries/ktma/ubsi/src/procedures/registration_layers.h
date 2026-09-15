#pragma once

#include "orbita_stand/scenario.h"

namespace orbita::stand {

// Private implementation layers. Only registerUbsiProcedures() is public;
// the delivery registrar composes these layers in one explicit order.
void registerLegacyUbsiProcedures(ScenarioEngine& engine);
void registerCurrentUbsiProcedures(ScenarioEngine& engine);
void registerRoktUbsiProcedures(ScenarioEngine& engine);
void registerV7UbsiProcedures(ScenarioEngine& engine);

} // namespace orbita::stand
