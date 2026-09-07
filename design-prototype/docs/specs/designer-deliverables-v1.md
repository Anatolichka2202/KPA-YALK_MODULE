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
- [ ] specialized single-product startup state prototype.
- [ ] multi-product error/loading variants.

### B. Core task flows

- [x] Station → КТМА.
- [x] КТМА → Production.
- [x] КТМА → Acceptance.
- [x] КТМА → Administration.
- [x] Production → active session.
- [x] Acceptance → active session.
- [x] ManualAction in active Acceptance session.
- [x] Safe Stop persistent control.
- [x] F12 engineering drawer preserves route.
- [ ] session recovery/reopen flow.
- [ ] explicit equipment-conflict resolution flow.
- [ ] stand-error recovery flow.

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
- [ ] ReportViewer formal component spec.
- [ ] EmptyState / ErrorState / PermissionState formal components.
- [ ] field/input/edit patterns for Administration.

### D. HMI and data visualization

- [x] overview for 96 channels.
- [x] selected channel state.
- [x] deviation axis.
- [x] actual value labels.
- [x] selected-channel trend.
- [x] consumption trend.
- [x] no 96 independent line charts.
- [ ] explicit NOT_NORMAL example on channel overview.
- [ ] stand telemetry/error overlay example.
- [ ] long-duration trend variant if method requires it.

### E. Operator safety and clarity

- [x] Safe Stop fixed in command bar.
- [x] dangerous action visually separated.
- [x] manual action requires explicit confirmation.
- [x] color duplicates text/icon.
- [x] busy resources show owner/session.
- [x] demo data labeled.
- [ ] confirmation pattern for irreversible configuration changes.
- [ ] permission-denied pattern.
- [ ] connection-lost / stale-data pattern.

### F. Content design

- [x] working-task-first labels.
- [x] standardized status labels.
- [x] distinction between operator and engineering terminology.
- [x] explicit wording that YALK/YTP-only demo does not prove full TU 5.6 compliance.
- [ ] content glossary with approved Russian terms.
- [ ] error-message grammar and remediation pattern.
- [ ] confirmation/notification grammar.

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
- [ ] run `npm build` for the branch.
- [ ] run existing Sites tests.
- [ ] visual QA against approved reference direction.
- [ ] record defects by severity P0/P1/P2.
- [ ] update `docs/qa/design-qa.md` only after verification.

## 3. Current implementation slice

Branch: `design/station-shell-v1`.

Implemented prototype files:

- `src/AppV1.jsx` — route orchestration and F12 state;
- `src/v1/designSystem.jsx` — reusable Station primitives;
- `src/v1/screens.jsx` — Station/КТМА prototype screens;
- `src/station-v1.css` — semantic industrial visual system;
- `docs/specs/station-shell-v1.md` — IA and states;
- `docs/specs/design-system-v1.md` — component/design-system contract.

Legacy `App.jsx` and `styles.css` are intentionally preserved for comparison/history; entry point is switched to v1 in this branch.

## 4. Designer decision log

### 07.09.2026 — IA reset

Decision: Production and TU Acceptance are working tasks, not global modes. Administration becomes the third normal KTMA task. F12 is reserved for engineering presentation/access.

### 07.09.2026 — common vs product-specific

Decision: shell, status semantics, safe-stop command bar, manual-action anatomy and chart anatomy are Station-level patterns. YALK/YTP/R4831/addresses/TU clauses remain KTMA-specific content.

### 07.09.2026 — data honesty

Decision: prototype numeric values are labeled as demo values unless they are explicitly backed by an approved method/source. Prototype UI must not convert partial automation coverage into a full normative verdict.
