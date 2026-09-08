# KTMA / UBSI application layer

This module is the product/application boundary for UBSI Production and TU orchestration.

## Boundary

UBSI application code may depend on:

- Registrar product/composition data;
- ScenarioEngine-facing scenario codes;
- generic stand capabilities (`ulk.parameter_source`, `stand.switch_matrix`, `power.dc_supply`, `signal.generator`, V7 measurement capabilities).

It must not depend on the Orbita/E20 product model:

- no `orbita.parameter_source` in current UBSI scenarios or catalog bindings;
- no requirement for an Orbita watch set;
- no E20 startup as a prerequisite for UBSI Production or TU;
- no `orbita::Orbita` object in this application layer.

`orbita/` is **not deprecated and must not be deleted**. It remains a reusable telemetry/diagnostic subsystem for future product deliveries, including BSI, RPU and other equipment whose acceptance/diagnostic path requires the Orbita/E20 data model. The architectural rule is product isolation: UBSI must not depend on Orbita, while another product package may intentionally compose Orbita capabilities.

## Production invariant

Every production run starts from a registered UBSI product and one production stage. Before every production run, the active composition must contain exactly one cell of each required type:

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
- `YVP` -> `YVP` plus the linked `YALK-96` output path.

For YVP the corrected YALK address range is **88..96**. YVP-8 still has eight measuring channels, therefore the exact eight-address binding inside that nine-address range is commissioning data. Active generator output stays blocked until the catalog contains eight unique confirmed addresses inside 88..96 and the configured map includes the confirmed range endpoints. No address is guessed by the application layer.

## Persistent lifecycle

`ProductionLedger` stores product-level production attempts in the same persistent SQLite data root (it can use `registrar.db`) without abusing a selected component as the owner of a full-block run.

Each record stores:

- product id and serial;
- one production stage;
- package and scenario code;
- immutable four-cell composition snapshot including SNs;
- affected-cell flags;
- ScenarioEngine `run_id` when available;
- timestamps;
- exact production status: `NORM`, `NOT_NORM`, `STAND_ERROR`, `INCOMPLETE`, `STOPPED`.

The dedicated status model prevents a technical stand failure or an incomplete run from being rewritten as a product failure or generic `Cancelled` state.
