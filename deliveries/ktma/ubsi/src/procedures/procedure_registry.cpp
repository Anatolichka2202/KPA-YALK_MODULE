#include "ktma/ubsi/procedures.h"
#include "registration_layers.h"

namespace orbita::stand {

void registerUbsiProcedures(ScenarioEngine& engine)
{
    // Composition order is part of the KTMA/UBSI delivery contract. Later
    // layers replace only the procedure ids for which this delivery has a
    // newer implementation. ISD-safe startup/cleanup replaces legacy global
    // reset behaviour first; the physical YALK layer is registered afterwards
    // so its confirmed open-input and overload choreography is authoritative.
    registerLegacyUbsiProcedures(engine);
    registerCurrentUbsiProcedures(engine);
    registerRoktUbsiProcedures(engine);
    registerIsdSafeUbsiProcedures(engine);
    registerYalkInitialPhysicalUbsiProcedures(engine);
    registerYalkPhysicalUbsiProcedures(engine);
    registerProductionFinalUbsiProcedures(engine);
    registerV7UbsiProcedures(engine);
    registerTuScopeUbsiProcedures(engine);
}

} // namespace orbita::stand
