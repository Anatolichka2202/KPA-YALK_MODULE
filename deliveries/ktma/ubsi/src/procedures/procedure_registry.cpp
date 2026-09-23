#include "ktma/ubsi/procedures.h"
#include "registration_layers.h"

namespace orbita::stand {

void registerUbsiProcedures(ScenarioEngine& engine)
{
    // Composition order is part of the KTMA/UBSI delivery contract. Later
    // layers replace only the procedure ids for which this delivery has a
    // newer implementation. The physical YALK layer intentionally replaces
    // the older ROKT overload callback with the frozen-donor-safe 80-channel
    // choreography while preserving the universal station runtime.
    registerLegacyUbsiProcedures(engine);
    registerCurrentUbsiProcedures(engine);
    registerRoktUbsiProcedures(engine);
    registerYalkPhysicalUbsiProcedures(engine);
    registerIsdSafeUbsiProcedures(engine);
    registerProductionFinalUbsiProcedures(engine);
    registerV7UbsiProcedures(engine);
    registerTuScopeUbsiProcedures(engine);
}

} // namespace orbita::stand
