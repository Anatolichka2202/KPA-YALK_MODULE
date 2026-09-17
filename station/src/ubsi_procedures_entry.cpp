#include "orbita_stand/ubsi_procedures.h"

namespace orbita::stand {

void registerRoktUbsiProcedures(ScenarioEngine& engine);
void registerPointMajorYalkProcedures(ScenarioEngine& engine);
void registerPowerLiveUbsiProcedures(ScenarioEngine& engine);
void registerProductionYvpProcedure(ScenarioEngine& engine);

void registerUbsiProcedures(ScenarioEngine& engine)
{
    // Preserve the current new-dis YALK/power overrides, then register the
    // finalized production YVP backend from master as the only YVP alias.
    registerRoktUbsiProcedures(engine);
    registerPointMajorYalkProcedures(engine);
    registerPowerLiveUbsiProcedures(engine);
    registerProductionYvpProcedure(engine);
}

} // namespace orbita::stand
