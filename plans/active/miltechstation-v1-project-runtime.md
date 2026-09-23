# MilTechStation V1 — project/workflow runtime

## Цель

Реализовать согласованную продуктовую модель, не останавливая доводку реального УБСИ:

```text
Project
  ↓
Workflow
  ↓
Scenario
  ↓
Resource / capability
  ↓
Equipment
  ↓
Evidence / result
```

Первый функциональный milestone остаётся практическим:

> реальный УБСИ проходит полный TU NORMAL через универсальный runtime и формирует доказуемый результат/протокол.

После этого та же платформа должна принять PPB project без PPB-specific сущностей в `station/`.

## Инварианты

- `station/` не получает KTMA/UBSI/YALK/YTP/YVP/PPB-specific модель;
- `Project` — композиционный корень продукта;
- workflow registration policy не принадлежит ScenarioEngine;
- free/TU/production используют общий runtime;
- физические resource bindings остаются в единственном StandProfile, а project package ссылается на него;
- published scenario не меняется in-place;
- scripts/HMI не обходят Resource/Plugin safety boundary;
- климатические setpoints не угадываются до подтверждённой методики;
- frozen donor `rebuild/tu-minimal-clean` не изменяется.

## Этап 1 — Project package contract

Статус: **IMPLEMENTED / CI PENDING**

Сделано:

- [x] `ProjectDefinition`;
- [x] `WorkflowDefinition`;
- [x] workflow registration policy;
- [x] `loadProjectPackage()`;
- [x] package validation;
- [x] reference workflow;
- [x] dynamic scenario flag для free workflow;
- [x] KTMA package `projects/ktma/project.yaml`;
- [x] workflow `free`;
- [x] workflow `tu_normal`;
- [x] workflow `tu_climate`;
- [x] workflow `production`;
- [x] workflow `production_climate`;
- [x] environment placeholders без выдуманных setpoints;
- [x] contract test `stand.project_definition`;
- [x] canonical product/reference documentation updated.

Остаётся:

- [ ] дождаться зелёного Windows CI;
- [ ] подключить selected Project к desktop composition вместо прямого выбора KTMA profile;
- [ ] добавить project identity в Run Context/Evidence.

## Этап 2 — Run Context и Evidence foundation

Статус: **NEXT**

- [ ] универсальный `RunContext`: project/workflow/DUT/operator/environment;
- [ ] backward-compatible ScenarioEngine entry point с RunContext;
- [ ] project/workflow identity в `ScenarioRunResult`;
- [ ] persistence в RunStore;
- [ ] базовый Evidence event envelope: sequence/time/type/resource/capability/operation/data;
- [ ] автоматический command/ack audit на EquipmentRegistry boundary;
- [ ] не смешивать registrar lifecycle и измерительный Evidence.

## Этап 3 — Resource ownership / safety

- [ ] ResourceLease для concurrent runs;
- [ ] симметричная блокировка Free/TU/Production при пересечении ресурсов;
- [ ] resource states READY/ACTIVE/SAFE/ERROR/INDETERMINATE;
- [ ] ISD timeout -> indeterminate semantics;
- [ ] operator recovery/restart UX;
- [ ] platform-level best-effort `safeStopAll`;
- [ ] equipment safety limits metadata.

## Этап 4 — KTMA project composition

- [ ] desktop выбирает/загружает project package;
- [ ] `equipment_profile` берётся из project package;
- [ ] workflow selector использует project workflows;
- [ ] current TU/Production behaviour сохраняется без big-bang rewrite;
- [ ] Free mode остаётся вне registrar;
- [ ] current KTMA readiness остаётся delivery-owned.

## Этап 5 — УБСИ TU NORMAL

Не менять нормативную методику ради архитектурного refactor.

- [ ] equipment readiness;
- [ ] power/readiness;
- [ ] ЯЛК полный тракт;
- [ ] overload ±12 V;
- [ ] ЯТП;
- [ ] ЯВП commissioning/physical path;
- [ ] TU coverage gate;
- [ ] Evidence + TU report;
- [ ] полный живой прогон.

## Этап 6 — Production

- [ ] registered DUT required;
- [ ] scopes/packages;
- [ ] production evidence;
- [ ] final TU reference run;
- [ ] normal/climate workflows.

## Этап 7 — Studio/Admin/HMI

После рабочего TU NORMAL:

- [ ] project browser;
- [ ] tree-based Scenario IR editor;
- [ ] properties/schema validation;
- [ ] YAML expert view поверх того же IR;
- [ ] declarative project screens;
- [ ] native custom screen API;
- [ ] Admin V1: project/equipment/connections/resources/diagnostics;
- [ ] calibration metadata без enforcement;
- [ ] roles model с выключенным enforcement.

## Этап 8 — Environment

- [ ] environment data model;
- [ ] continuous environment evidence;
- [ ] manual climate flow;
- [ ] controlled chamber capability;
- [ ] post-climate workflow;
- [ ] vibration capability;
- [ ] parallel exposure + monitoring.

## Этап 9 — Script runtime / PPB

- [ ] Station Script API;
- [ ] LuaHost;
- [ ] production script identity/hash/runtime;
- [ ] разобрать PPB на project/scenario/Lua/generator;
- [ ] генератор только через `signal.generator`;
- [ ] migration без отдельного PPB application;
- [ ] architecture-leak regression: PPB не добавляет PPB-specific code в core.

## Не входит в V1

- полноценная Fleet/СУС;
- hard-real-time HIL executor;
- web dashboard;
- сложный graph canvas;
- MES;
- mandatory calibration enforcement;
- полноценный RBAC.

## Изменённый код / данные на текущем этапе

- `station/include/orbita_stand/project.h`
- `station/src/project.cpp`
- `station/tests/project_definition_test.cpp`
- `station/CMakeLists.txt`
- `projects/ktma/project.yaml`
- `projects/ktma/workflows/*.yaml`
- `projects/ktma/environments/*.yaml`

## Изменённые документы

- `docs/product/miltechstation.md`
- `docs/reference/components/project-package.md`
- `docs/reference/INDEX.md`
- этот план.
