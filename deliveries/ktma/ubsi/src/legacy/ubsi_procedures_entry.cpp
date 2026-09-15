#include "orbita_stand/ubsi_procedures.h"

namespace orbita::stand {

void registerRoktUbsiProcedures(ScenarioEngine& engine);
void registerYvpV7Procedures(ScenarioEngine& engine);

void registerUbsiProcedures(ScenarioEngine& engine)
{
    // Historical/current callbacks remain compiled and registered first.
    // The final production alias ubsi.yvp is then replaced by the V7+ISD
    // implementation. This preserves the previous work without selecting it
    // for production.
    registerRoktUbsiProcedures(engine);
    registerYvpV7Procedures(engine);
}

} // namespace orbita::stand
