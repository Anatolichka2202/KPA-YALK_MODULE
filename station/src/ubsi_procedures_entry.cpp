#include "orbita_stand/ubsi_procedures.h"

namespace orbita::stand {

void registerRoktUbsiProcedures(ScenarioEngine& engine);
void registerIsdSafeUbsiProcedures(ScenarioEngine& engine);
void registerProductionFinalUbsiProcedures(ScenarioEngine& engine);
void registerProductionYvpProcedure(ScenarioEngine& engine);

void registerUbsiProcedures(ScenarioEngine& engine)
{
    // Keep the validated YALK/current delivery stack, then layer the safe ISD
    // implementation and the final production lifecycle/YTP overrides on top.
    // Production YVP remains the single final ubsi.yvp implementation.
    registerRoktUbsiProcedures(engine);
    registerIsdSafeUbsiProcedures(engine);
    registerProductionFinalUbsiProcedures(engine);
    registerProductionYvpProcedure(engine);
}

} // namespace orbita::stand
