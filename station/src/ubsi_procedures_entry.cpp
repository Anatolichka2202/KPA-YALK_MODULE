#include "orbita_stand/ubsi_procedures.h"

namespace orbita::stand {

void registerRoktUbsiProcedures(ScenarioEngine& engine);
void registerIsdSafeUbsiProcedures(ScenarioEngine& engine);
void registerProductionYvpProcedure(ScenarioEngine& engine);

void registerUbsiProcedures(ScenarioEngine& engine)
{
    // Preserve the current YALK/YTP delivery logic, then override only the ISD
    // entry points whose old semantics relied on firmware type=4/type=7.
    // Production YVP remains the single final ubsi.yvp implementation.
    registerRoktUbsiProcedures(engine);
    registerIsdSafeUbsiProcedures(engine);
    registerProductionYvpProcedure(engine);
}

} // namespace orbita::stand
