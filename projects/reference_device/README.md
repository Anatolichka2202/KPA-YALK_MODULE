# Reference device package — portability fixture

This package is a **simulation-only** acceptance fixture for the universal MilTechStation stack. It represents neither KTMA/UBSI nor a real BRP device. It never authorizes electrical outputs.

## Package contract

- `project.yaml` declares the project identity, DUT type, catalog, profile and workflow.
- `workflows/generic.yaml` exposes the common `station.generic` HMI action.
- `scenarios/reference_voltage.yaml` calls the generic `parameter.measure` procedure.
- `../../data/profiles/stand_reference_simulator.yaml` declares a `parameter_source` simulator, explicitly disabling active outputs.
- `../../data/catalog/catalog_reference.yaml` is a separate catalog owned by this package.

To select this package in a complete desktop installation, set the `MILTECH_PROJECT` environment variable to the **absolute path** of `projects/reference_device/project.yaml` before launching `MilTechStation`. The installed project directory and referenced `data/` assets must be present together.

The reference measurement uses a simulated value of 12 V, an expected unit of `В`, and an example acceptance interval of 10–14 V. These are fixture data, **not BRP specifications or safety limits**.

## Introducing another device

1. Copy the project package under a new directory and assign a unique project ID and DUT type.
2. Supply a device-specific catalog and component profile; the profile defines source capabilities and physical bindings.
3. Implement or configure the necessary `IParameterSource` / equipment plugin. The common scenario engine must not contain physical transport code.
4. Express the methodology as versioned YAML scenarios using generic registered procedures where possible. Register device-specific procedures in the delivery layer only when necessary.
5. Bind a supported operator action: `station.generic` for parameter/procedure checks, or `station.free` for the live workspace. `ktma.*` actions belong to the KTMA package and are not available in other projects.
6. Validate simulations (including errors, no data, interrupted execution and safe stop) before any physical output can be enabled.

Do not assume that a passing simulator test validates a real electrical connection, device protocol, a BRP procedure or a specialized DUT HMI. The common Qt desktop currently retains some KTMA inheritance; this reference package tests **runtime and scenario/HMI routing reuse**, not final architecture independence.

## Regression checks

`stand.project_definition` verifies the independent project package and executes its generic simulated measurement through `runProjectWorkflow`. `desktop.station_gui_simulator` checks that the reference project is opened with its own title, scenario and parameter monitoring, without KTMA production actions.
