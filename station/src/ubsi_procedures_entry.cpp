#include "orbita_stand/ubsi_procedures.h"

namespace orbita::stand {

void registerRoktUbsiProcedures(ScenarioEngine& engine);
void registerPowerLiveUbsiProcedures(ScenarioEngine& engine);
void registerYvpV7Procedures(ScenarioEngine& engine);

void registerUbsiProcedures(ScenarioEngine& engine)
{
    // Historical/current callbacks remain compiled and registered first.
    // Operator-facing replacements then add evidence required by the HMI,
    // without changing the normative product verdict. The final production
    // alias ubsi.yvp is owned by the confirmed V7+ISD backend.
    registerRoktUbsiProcedures(engine);
    registerPowerLiveUbsiProcedures(engine);
    registerYvpV7Procedures(engine);
}

} // namespace orbita::stand
