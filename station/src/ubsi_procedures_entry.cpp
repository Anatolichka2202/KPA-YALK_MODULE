#include "orbita_stand/ubsi_procedures.h"

namespace orbita::stand {

void registerRoktUbsiProcedures(ScenarioEngine& engine);
void registerProductionYvpProcedure(ScenarioEngine& engine);

void registerUbsiProcedures(ScenarioEngine& engine)
{
    // Register current YALK/YTP delivery procedures, then the only YVP backend.
    registerRoktUbsiProcedures(engine);
    registerProductionYvpProcedure(engine);
}

} // namespace orbita::stand
