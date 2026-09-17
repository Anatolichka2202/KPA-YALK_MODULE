# MilTechStation: station / delivery / product boundaries

Status: architecture baseline for production integration.

## 1. Dependency axis

The dependency direction is one-way:

```text
station platform + station UI
            ↓
      delivery package
      + delivery UI
            ↓
       product module
       + product UI
```

A product UI may depend on its delivery UI contract. A delivery UI may depend on
station UI contracts. The station must not depend on KTMA, UBSI, PPB or another
product.

KTMA is a delivery. UBSI is the first production product module inside the KTMA
delivery. PPB may arrive with its own complete UI and does not have to look like
UBSI.

## 2. UI ownership

### Station UI

Owns only platform functions:

- application shell and common navigation;
- administration shell;
- logs, diagnostics and run/evidence browser;
- generic equipment/runtime state;
- artifact browser/editor host;
- provider registries for documents, scenarios and execution runtimes.

It does not own UBSI workflow, KTMA registrar semantics or PPB pages.

### Delivery UI

Owns delivery-wide functions and services. For KTMA this includes the place for
the complete KTMA composition: stand profile, registrar integration, equipment
service views and product registration.

A delivery decides which product modules are built into the package and which
optional runtimes are allowed.

### Product UI

Owns product-specific operator UX. UBSI owns Production/TU workspaces and its
Power/YALK/YTP/YVP views. These remain product UI and are not generalized into a
YAML-generated screen.

PPB can keep a product-specific interface. Shared controls are moved to station
UI only after a second real consumer proves that they are generic.

## 3. Administration and artifact formats

Administration is a station function, but file formats are providers, not the
station architecture.

Artifacts are separated by semantics:

```text
document/config     -> codec/editor/validator
scenario            -> scenario provider
executable script   -> execution runtime
binary/data         -> viewer/importer
```

Target provider model:

- YAML: current ScenarioEngine/document provider;
- JSON: Qt JSON document provider;
- INI: QSettings-backed provider;
- TOML: optional provider; planned dependency is toml++;
- TXT: plain-text provider with no invented schema;
- Lua: executable runtime/provider, disabled unless a delivery enables it;
- Python: external-process integration first; embedding is not required for the
  first integration.

A generic raw-text editor is allowed for textual artifacts. A typed editor is an
optional delivery/product contribution. The station must not silently reinterpret
INI/TOML/JSON/TXT as YAML.

## 4. Scenario boundary

Scenario format and run lifecycle are separate concerns.

```text
scenario artifact
      ↓
ScenarioProvider
      ↓
common run request / events / result / evidence
```

The existing YAML ScenarioEngine is one provider. A future PPB Lua scenario
provider may execute Lua while publishing the same run/evidence lifecycle.
External Python software can be connected through the process runtime rather
than rewritten into C++.

Scripts are disabled by default. Delivery composition explicitly enables a
runtime; this is where PPB may enable Lua while KTMA/UBSI does not need to.

## 5. Technology stack

Current production base:

- C++17;
- Qt 6 / Qt Widgets;
- CMake;
- yaml-cpp for existing YAML scenarios/config;
- SQLite-based registrar/report/run data already used by the project;
- equipment plugins behind the station equipment ABI.

Planned platform providers:

- Qt JSON for JSON;
- QSettings for INI;
- toml++ for TOML when the first real TOML consumer is connected;
- Lua 5.4 as an optional delivery-enabled runtime;
- Python 3 as an external process first;
- plain QFile/QTextStream path for TXT.

The `universal_miltechstation` branch is a donor for StationSession,
component/resource composition and process execution runtime. It is not merged
wholesale into the production branch.

## 6. Repository ownership target

```text
platform/
  ui/                       station-facing UI contracts

apps/desktop/               concrete station desktop shell

deliveries/ktma/
  ui/                       KTMA delivery UI contract/composition
  registrar/
  ubsi/
    backend/domain
    ui/                     UBSI product UI module

station/                    reusable runtime/equipment/scenario infrastructure
```

The first implementation slice introduces the compile-time
`station -> KTMA delivery -> UBSI product` manifest boundary without changing
the approved UBSI pixels or operator workflow.
