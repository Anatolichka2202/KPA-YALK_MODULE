# Station / delivery / product UI migration

Base branch: `integration/master-yvp-ui`
Working branch: `architecture/platform-delivery-ui`

## Invariants

- master remains production backend truth for UBSI;
- approved UBSI UI is not redesigned by this migration;
- dependency direction is station -> delivery -> product;
- product UI may depend on delivery UI; station UI never depends on product UI;
- administration belongs to station, formats/runtimes are registered providers;
- executable runtimes are disabled unless enabled by delivery composition;
- `universal_miltechstation` and `isd_driver_safe` are donors, not wholesale
  merge targets.

## Phase 1 — composition boundary

- [x] add generic station UI manifest contract;
- [x] add KTMA delivery UI target;
- [x] add UBSI product UI module target;
- [x] make desktop compose KTMA + UBSI at compile time;
- [x] add contract test that KTMA contains UBSI;
- [x] build/configure and the new `ktma.ubsi.ui_contract` test pass in CI;
- [ ] full branch CI is currently blocked by the inherited
  `desktop.test_page_tu_runtime` failure from the base integration branch
  (same failure exists at base commit `55a449f`).

## Phase 2 — physical UBSI UI ownership

- [ ] move `ubsi_ui_model`, measurement views, TU flow and TestPage implementation
  behind `ktma_ubsi_ui`;
- [ ] keep only station-generic widgets in desktop core;
- [ ] preserve frozen Production/TU behavior and acceptance tests;
- [ ] make UBSI UI consume backend RunEvent/ScenarioRunResult only.

## Phase 3 — station administration

- [ ] replace YAML-specific administration entry point with ArtifactRegistry;
- [ ] add DocumentProvider contract (load/save/validate/syntax metadata);
- [ ] register YAML first without changing existing scenario semantics;
- [ ] add JSON, INI and TXT providers;
- [ ] add TOML only with the first real TOML consumer;
- [ ] allow delivery/product typed editors without removing raw-text fallback.

## Phase 4 — scenario/runtime providers

- [ ] introduce ScenarioProvider boundary over common run/evidence lifecycle;
- [ ] keep YAML ScenarioEngine as provider #1;
- [ ] port process execution runtime from `universal_miltechstation`;
- [ ] integrate the existing Python program as an external process;
- [ ] add optional Lua 5.4 provider for PPB and enable it only in PPB composition.

## Phase 5 — KTMA delivery completion

- [ ] integrate stateful ISD provider from `isd_driver_safe`;
- [ ] port StationSession/component/resource model in small tested slices;
- [ ] make KTMA own stand profile, registrar, equipment readiness and all KTMA
  product registration;
- [ ] leave explicit product slots for later KTMA modules besides UBSI.

## Phase 6 — remove transitional inheritance

- [ ] replace `KtmaMainWindow : MainWindow` hardware/product access with
  composition;
- [ ] shrink and remove protected `integration*()` escape hatches;
- [ ] station desktop shell receives delivery contributions through contracts;
- [ ] no `if (ktma)` / `if (ubsi)` in reusable station UI.

## Acceptance

A second delivery/product (PPB is the intended proof) must be attachable without
copying MainWindow or adding PPB branches to station core. PPB may keep its own
operator interface and enable Lua while reusing only the platform pieces it
actually needs.
