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

Статус: **IMPLEMENTED / GREEN**

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
- [x] canonical product/reference documentation updated;
- [x] Windows CI после project/runtime slice прошёл полностью.

## Этап 2 — Run Context и Evidence foundation

Статус: **PARTIAL / ACTIVE**

Сделано:

- [x] `ProjectRunContext` для DUT/operator/project-level attributes;
- [x] `runProjectWorkflow()` поверх существующего `ScenarioEngine` без второго движка;
- [x] workflow registration gate до физического запуска;
- [x] dynamic scenario разрешён только соответствующим workflow;
- [x] project/workflow/DUT/operator/environment identity в `ScenarioRunResult`;
- [x] additive SQLite migration `RunStore` для project/workflow context;
- [x] сохранение project/workflow context и attributes в `test_runs`;
- [x] regression `stand.run_store_context`;
- [x] legacy raw scenario API остаётся совместимым: новые поля у таких run пустые;
- [x] registrar lifecycle не перенесён в station core;
- [x] общий audited scenario runner пишет COMMAND / COMMAND_ACK / ERROR / SAFETY на equipment boundary;
- [x] project/free и raw scenario path используют один механизм исполнения, без отдельного FreeModeEngine.

Остаётся:

- [ ] resource/device/quality identity для measurement evidence;
- [ ] связать delivery readiness с общим run/evidence lifecycle;
- [ ] хранение high-rate/raw artifacts привязать к Evidence metadata.

## Этап 3 — Resource ownership / safety

Статус: **PARTIAL / ACTIVE**

Сделано:

- [x] ISD driver поддерживает owner-scoped mutations и `release_owner`;
- [x] generic ISD safe-stop не использует firmware type=4;
- [x] YALK overload использует отдельные ownership domains для background DAC и transient ±12 V impact;
- [x] аварийный cleanup перегрузки адресный и не требует global reset.

Остаётся:

- [ ] ResourceLease для concurrent runs;
- [ ] симметричная блокировка Free/TU/Production при пересечении ресурсов;
- [ ] resource states READY/ACTIVE/SAFE/ERROR/INDETERMINATE;
- [ ] ISD timeout -> indeterminate semantics в общем resource state;
- [ ] operator recovery/restart UX;
- [ ] equipment safety limits metadata.

## Этап 4 — KTMA project composition

Статус: **PARTIAL / ACTIVE**

Сделано:

- [x] build runtime сохраняет `projects/` и package-relative `data/` рядом с приложением;
- [x] `UniversalMainWindow` загружает `ProjectDefinition` из `MILTECH_PROJECT` либо `projects/ktma/project.yaml`;
- [x] заголовок и log получают identity выбранного project package;
- [x] Free workflow выполняется через `runProjectWorkflow()` и получает project/workflow context;
- [x] Free mode остаётся вне registrar;
- [x] отсутствие package имеет compatibility fallback на старый raw ScenarioEngine path.

Остаётся:

- [ ] базовый desktop composition больше не должен наследовать project selection от `KtmaMainWindow`;
- [ ] `equipment_profile` основной KTMA runtime должен браться из project package, а не из legacy `profiles/stand_ktma.yaml` выбора;
- [ ] TU/Production launcher перевести на project workflows с сохранением текущего UX;
- [ ] current KTMA readiness оставить delivery-owned;
- [ ] workflow selector сделать project-driven вместо hard-coded codes.

## Этап 5 — УБСИ TU NORMAL

Не менять нормативную методику ради архитектурного refactor.

### ЯЛК / перегрузка — текущий slice

Статус: **CODED / CI PENDING / LIVE RUN REQUIRED**

Сделано:

- [x] frozen donor прочитан только как read-only физический reference;
- [x] подтверждённая безопасная карта ЯЛК: `1-28,32-43,45-70,74-87`;
- [x] линии `29/30/31/44/71/72/73/88`, занятые ЯВП, исключаются из полного overload routing;
- [x] legacy `physical_channel_count=88` больше не означает физическое воздействие на все линии 1..88: runtime переводит полный проход на безопасную 80-канальную карту;
- [x] baseline строится на безопасных 80 DAC routes;
- [x] background не пересоздаётся для каждого воздействия;
- [x] перед ±12 В отключается только DAC целевого канала;
- [x] после `dac_off_settle` включается общий источник `+12`/`-12` и type=3 target;
- [x] после выдержки читается только fresh snapshot, anchored after live `last_sequence`;
- [x] сравниваются только остальные safe observed addresses;
- [x] после воздействия target/common type=3 снимаются адресно, DAC цели восстанавливается;
- [x] transient impact и background имеют разные ISD owners;
- [x] firmware type=4/global reset внутри overload sequence не используется;
- [x] сохранён текущий master criterion `abs(delta) <= 2 code`;
- [x] сохранён текущий master `overload_settle_ms=10000`; donor `1000 ms` не переносится без нового подтверждения;
- [x] добавлен отдельный regression `ktma.ubsi.yalk_overload_physical`, фиксирующий порядок команд и freshness.

Остаётся:

- [ ] получить зелёный CI этого slice;
- [ ] сделать explicit safe-address args в canonical scenario вместо compatibility `*_count=88`;
- [ ] синхронизировать canonical testing/memory после зелёного CI;
- [ ] выполнить новый полный живой overload run актуального master;
- [ ] по результату живого прогона отдельно решить timing/criterion, не копируя donor автоматически.

### Остальной TU NORMAL

- [ ] equipment readiness end-to-end evidence;
- [ ] power/readiness;
- [ ] ЯЛК полный analog/contact/open/reference тракт проверить на master после overload merge;
- [ ] ЯТП;
- [ ] ЯВП commissioning/physical path;
- [ ] TU coverage gate;
- [ ] Evidence + TU report;
- [ ] полный живой прогон.

## Этап 6 — Production

- [ ] registered DUT required через project workflow;
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

Platform/project slice:

- `station/include/orbita_stand/project.h`
- `station/src/project.cpp`
- `station/include/orbita_stand/scenario.h`
- `station/src/run_store.cpp`
- `station/tests/project_definition_test.cpp`
- `station/tests/run_store_context_test.cpp`
- `station/CMakeLists.txt`
- `apps/desktop/CMakeLists.txt`
- `apps/desktop/universal_mainwindow.h`
- `apps/desktop/universal_mainwindow.cpp`
- `projects/ktma/project.yaml`
- `projects/ktma/workflows/*.yaml`
- `projects/ktma/environments/*.yaml`

Current YALK physical slice:

- `deliveries/ktma/ubsi/src/procedures/yalk_overload_physical.cpp`
- `deliveries/ktma/ubsi/src/procedures/registration_layers.h`
- `deliveries/ktma/ubsi/src/procedures/procedure_registry.cpp`
- `deliveries/ktma/ubsi/tests/yalk_overload_physical_test.cpp`
- `deliveries/ktma/ubsi/CMakeLists.txt`

## Изменённые документы

- `docs/product/miltechstation.md`
- `docs/product/data.md`
- `docs/reference/components/project-package.md`
- `docs/reference/INDEX.md`
- этот план.
