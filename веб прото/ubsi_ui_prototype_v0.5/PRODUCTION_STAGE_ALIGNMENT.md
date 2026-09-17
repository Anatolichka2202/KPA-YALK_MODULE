# Production stage alignment

Source-of-truth checked against current repository `deliveries/ktma/registrar/include/model.h` and `docs/delivery/administration.md`.

Current production stages: `Primary`, `ClimateNormal`, `ClimateMinus`, `ClimatePlus`, `PottingClimateNormal`, `PottingClimatePlus`, `PottingClimateMinus`.

Legacy values (`InitialElectrical`, `PostVibrationElectrical`, `PostClimateElectrical`, `FinalElectrical`) remain readable for historical registrar data but are not offered as new Production Session stages.

UI policy from the merged design spec §9.5: the stage applies to the whole session; human-readable names come from delivery configuration; temperatures must not be inferred from enum/code.
