#include "ktma/ubsi/procedures.h"
#include "registration_layers.h"

namespace orbita::stand {

void registerUbsiProcedures(ScenarioEngine& engine)
{
    // Composition order is part of the KTMA/UBSI delivery contract. Later
    // layers replace only the procedure ids for which this delivery has a
    // newer implementation; safety/finalization own the current lifecycle and V7 owns ubsi.yvp.
    registerLegacyUbsiProcedures(engine);
    registerCurrentUbsiProcedures(engine);
    registerRoktUbsiProcedures(engine);
    registerIsdSafeUbsiProcedures(engine);
    registerProductionFinalUbsiProcedures(engine);
    registerV7UbsiProcedures(engine);
}

} // namespace orbita::stand
