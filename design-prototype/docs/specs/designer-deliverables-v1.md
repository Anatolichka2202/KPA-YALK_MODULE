# Designer Deliverables / КТМА Prototype Program

Назначение: рабочий чек-лист обязанностей дизайнера для `design-prototype/`.

## 1. Границы ответственности

Дизайнер работает с:

- информационной архитектурой;
- пользовательскими рабочими задачами и flow;
- shell МилТех Станции;
- визуальным языком и design system;
- HMI-компонентами;
- состояниями, ошибками, busy/empty/manual-action;
- operator safety UX;
- прототипами экранов и взаимодействий;
- контентом интерфейса;
- accessibility/keyboard behavior;
- визуальным QA и handoff-спецификацией для будущего Qt UI.

Дизайнер не изменяет:

- измерительные алгоритмы;
- backend и хранение данных;
- `orbita/`;
- C++ production code;
- Qt production implementation;
- аппаратные протоколы;
- нормативные допуски без подтверждённого источника.

## 2. Обязательные артефакты

### A. Product architecture

- [x] MilTech → Station → Products hierarchy.
- [x] Station Home anatomy.
- [x] КТМА: Production / TU Acceptance / Administration.
- [x] F12 = engineering layer, independent from business task.
- [x] route map.
- [x] state matrix products/sessions/equipment.
- [x] specialized single-product startup state prototype.
- [x] multi-product loading/error variants.
- [ ] explicit empty launcher state: no available products.

### B. Core task flows

- [x] Station → КТМА.
- [x] КТМА → Production.
- [x] КТМА → Acceptance.
- [x] КТМА → Administration.
- [x] Production → active session → production ledger.
- [x] Acceptance → active session → acceptance ReportViewer.
- [x] ManualAction in active Acceptance session.
- [x] Safe Stop persistent control.
- [x] F12 engineering drawer preserves route.
- [x] session recovery/reopen UX prototype after UI restart.
- [x] explicit equipment-conflict resolution prototype.
- [x] stand-error recovery prototype.

### C. Design system

- [x] semantic colors/tokens.
- [x] status vocabulary.
- [x] ProductHeader.
- [x] StatusBadge.
- [x] Panel.
- [x] CommandButton.
- [x] SessionCommandBar.
- [x] ManualAction anatomy.
- [x] ActiveSessionRow / EquipmentRow anatomy.
- [x] ChannelOverview anatomy.
- [x] SelectedChannelTrend.
- [x] ConsumptionTrend.
- [x] ReportViewer prototype + report hierarchy.
- [x] Error/recovery presentation pattern.
- [x] Permission-denied presentation pattern.
- [x] field/input/edit patterns for Administration.

### D. HMI and data visualization

- [x] overview for 96 channels.
- [x] selected channel state.
- [x] deviation axis.
- [x] actual value labels.
- [x] selected-channel trend.
- [x] consumption trend.
- [x] no 96 independent line charts.
- [x] explicit NOT_NORMAL example directly on channel overview.
- [x] stand telemetry/error overlay directly on active HMI.
- [x] stale-data overlay directly on active HMI.
- [ ] long-duration trend variant if approved method requires it.

### E. Operator safety and clarity

- [x] Safe Stop fixed in command bar.
- [x] dangerous action visually separated.
- [x] Safe Stop remains available while ManualAction blocks ordinary commands.
- [x] manual action requires explicit confirmation.
- [x] color duplicates text/icon.
- [x] busy resources show owner/session in conflict scenario.
- [x] demo data labeled.
- [x] confirmation pattern for composition-changing configuration action.
- [x] permission-denied pattern.
- [x] connection-lost / stale-data pattern.
- [x] restart/reopen UX does not silently create a new RUN.

### F. Content design

- [x] working-task-first labels.
- [x] standardized status labels.
- [x] distinction between operator and engineering terminology.
- [x] explicit wording that YALK/YTP-only demo does not prove full TU 5.6 compliance.
- [x] content glossary / approved wording baseline in `content-language-v1.md`.
- [x] error-message grammar and remediation pattern.
- [x] confirmation/notification grammar baseline.

### G. Responsive / target environments

- [x] reference target declared: 1920×1080.
- [x] regression targets declared: 1664×935, 1024×768, 720×900.
- [x] CSS responsive rules implemented.
- [ ] visual screenshot QA on 1920×1080.
- [ ] visual screenshot QA on regression targets.
- [ ] keyboard-only pass.

### H. QA / Handoff

- [x] design-system Definition of Done.
- [x] source-of-truth hierarchy documented.
- [x] separate v1 QA plan created; legacy QA is not reused as proof.
- [x] known defects recorded by P0/P1/P2 and tracked explicitly.
- [x] diff scope checked: current branch changes stay under `design-prototype/`.
- [ ] run `npm run build` for the branch.
- [ ] run existing Sites tests.
- [ ] visual QA against approved reference direction.
- [ ] accessibility focus-trap/return verification for modal flows.
- [ ] update QA status to `passed` only after actual verification.

## 3. Current implementation slice

Branch: `design/station-shell-v1`.

Implemented prototype files:

- `src/AppV1.jsx` — route orchestration and F12 state;
- `src/v1/designSystem.jsx` — reusable Station primitives and semantic statuses;
- `src/v1/screens.jsx` — Station/КТМА base screens retained as the first v1 slice;
- `src/v1/sessionsV2.jsx` — connected Acceptance session → ReportViewer;
- `src/v1/productionSessionV2.jsx` — Production HMI → production ledger + active HMI fault/stale states;
- `src/v1/scenarios.jsx` — single-product, recovery, ReportViewer and Administration edit scenarios;
- `src/v1/edgeCasesV2.jsx` — multi-product, permission and session-reopen edge cases;
- `src/v1/scenarioDockV2.jsx` — React-prototype-only scenario navigator;
- `src/v1/workspaces.jsx` — interactive Administration workspace;
- `src/station-v1.css` — semantic industrial visual system;
- `src/station-v1-safety.css` — safety-layer behavior around ManualAction;
- `src/station-v1-scenarios.css` — scenario states;
- `src/station-v1-workspaces.css` — operational Administration workspace;
- `src/station-v1-edge.css` — Station-level edge cases;
- `src/station-v1-hmi-states.css` — active HMI NOT_NORMAL / STALE / stand-error overlays;
- `docs/specs/station-shell-v1.md` — IA and states;
- `docs/specs/design-system-v1.md` — component/design-system contract;
- `docs/specs/content-language-v1.md` — operator wording and message grammar;
- `docs/specs/error-recovery-v1.md` — recovery semantics;
- `docs/specs/operational-scenarios-v1.md` — implemented scenario matrix.

Legacy `App.jsx` and `styles.css` are intentionally preserved for comparison/history; entry point is switched to v1 in this branch.

## 4. Designer decision log

### 07.09.2026 — IA reset

Decision: Production and TU Acceptance are working tasks, not global modes. Administration becomes the third normal KTMA task. F12 is reserved for engineering presentation/access.

### 07.09.2026 — common vs product-specific

Decision: shell, status semantics, safe-stop command bar, manual-action anatomy and chart anatomy are Station-level patterns. YALK/YTP/R4831/addresses/TU clauses remain KTMA-specific content.

### 07.09.2026 — data honesty

Decision: prototype numeric values are labeled as demo values unless they are explicitly backed by an approved method/source. Prototype UI must not convert partial automation coverage into a full normative verdict.

### 07.09.2026 — report separation

Decision: Production ledger and TU Acceptance protocol are different UX artifacts. A completed production stage must not visually imply a full acceptance verdict.

### 07.09.2026 — recovery semantics

Decision: `НЕ НОРМА`, `ОШИБКА`, `ДАННЫЕ УСТАРЕЛИ` and `ЗАНЯТО` have different causes and different recovery actions; they must not be collapsed into one red warning state.

### 07.09.2026 — access semantics

Decision: F12 changes engineering presentation/tools, but does not grant permissions. Permission denial is an independent state and must preserve the current working task.

### 07.09.2026 — restart semantics

Decision: after restart, an unfinished session must be surfaced explicitly. UI must not silently start a replacement RUN or treat pre-restart measurements as fresh data.