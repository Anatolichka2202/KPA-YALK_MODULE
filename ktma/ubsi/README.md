# KTMA / UBSI application layer

This module is the product/application boundary for UBSI Production and TU orchestration.

## Boundary

UBSI application code may depend on:

- Registrar product/composition data;
- ScenarioEngine-facing scenario codes;
- generic stand capabilities (`ulk.parameter_source`, `stand.switch_matrix`, `power.dc_supply`, `signal.generator`, V7 measurement capabilities).

It must not depend on the legacy Orbita/E20 product model:

- no `orbita.parameter_source` in current UBSI scenarios or catalog bindings;
- no requirement for an Orbita watch set;
- no E20 startup as a prerequisite for Production or TU;
- no `orbita::Orbita` object in this application layer.

The `orbita/` directory remains a legacy telemetry subsystem and can continue to support engineering/legacy screens while KTMA/UBSI migrates away from it.

## Production invariant

Every production run starts from a registered UBSI product and one production stage. Before the first and every subsequent production run, the active composition must contain exactly one cell of each required type:

- `YALK-96`;
- `YTP`;
- `YVP`;
- `YP-P`.

The selected package determines affected cells, not whether the rest of the product composition may be absent.

Package effects:

- `FULL_UBSI` -> all four cells;
- `POWER_CONSUMPTION` -> `YP-P`;
- `YALK` -> `YALK-96`;
- `YTP` -> `YTP`;
- `YVP` -> `YVP` plus linked `YALK-96` output path (YALK addresses 89..96).
