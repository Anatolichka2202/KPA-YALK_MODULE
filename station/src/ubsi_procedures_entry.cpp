#include "orbita_stand/ubsi_procedures.h"

namespace orbita::stand {

void registerRoktUbsiProcedures(ScenarioEngine& engine);
void registerPointMajorYalkProcedures(ScenarioEngine& engine);
void registerPowerLiveUbsiProcedures(ScenarioEngine& engine);
void registerYvpV7Procedures(ScenarioEngine& engine);

void registerUbsiProcedures(ScenarioEngine& engine)
{
    // Historical/current callbacks remain compiled and registered first.
    // Operator-facing replacements then enforce the accepted scan order and
    // add UI evidence without changing the normative criteria. The final
    // production alias ubsi.yvp is owned by the confirmed V7+ISD backend.
    registerRoktUbsiProcedures(engine);
    registerPointMajorYalkProcedures(engine);
    registerPowerLiveUbsiProcedures(engine);
    registerYvpV7Procedures(engine);
}

} // namespace orbita::stand
