# MilTechStation V1 — project/workflow runtime

## Рабочая спецификация и задачи

Согласованная продуктовая спецификация и декомпозиция живут в
[`_specs/miltechstation-v1/spec.md`](../../_specs/miltechstation-v1/spec.md) и
[`task.md`](../../task.md). Статусы задач являются источником истины только в
`_specs/miltechstation-v1/tasks/`; этот план сохраняет архитектурные решения и
текущие факты, а не дублирует прогресс.

## Аудит общего слоя станции (2026-09-28)

Этот срез **не включает** физический тракт КТМА/УБСИ. `DONE` ниже означает
завершённую ограниченную подзадачу, а не готовность продукта к выпуску.

| Подзадача | Текущий статус | Проверяемое основание / граница |
| --- | --- | --- |
| Контракт project package и workflow | `CONTRACT_TESTED`; локальный полный CTest зелёный | `station.project_definition` проходит; `station/src/project.cpp`, `station/tests/project_definition_test.cpp`. Удалённый CI этим локальным запуском не подтверждён. |
| Декларативный профиль, component lifecycle и сессия | `CONTRACT_TESTED` для имеющихся контрактов | `stand.component_profile`, `stand.component_runtime`, `stand.station_session` проходят; `station/src/station_session.cpp`. Это не подтверждение произвольного нового оборудования на стенде. |
| Внешний process execution runtime | `CONTRACT_TESTED` | `stand.execution_runtime` и `stand.execution_scenario` проходят; Evidence и artifact persistence покрыты задачей 06. Production consumer понадобится отдельной delivery-задаче, если появится. |
| Источник сырых отсчётов и интеграционная граница | `CONTRACT_TESTED` для текущего sample bridge | `integration.orbita_sample_bridge` проходит; поддержка других источников не доказана этим тестом. |
| Resource/capability routing | `CONTRACT_TESTED` | `stand.scenario_resource` проходит после исправления UTF-8 пути временного YAML через Qt file API; fixture принудительно использует кириллический путь. |
| Project workflow run и сохранение контекста | `CONTRACT_TESTED` | `stand.project_definition` и `stand.run_store_context` проходят; последний выполняет generic run → SQLite save/load → HTML re-render. Measurement provenance хранит `PROVIDED` либо явный `NOT_PROVIDED`; `RunArtifacts` прикрепляет content-addressed metadata telemetry/raw packets. |
| Защита ресурсов и восстановление | `CONTRACT_TESTED`; driver confirmation — release/bench gate | `ResourceLeaseManager` хранит READY/ACTIVE/SAFE/ERROR/INDETERMINATE и trace; сценарий требует подтверждение `safeStopResources`, otherwise результат не может быть OK и ресурс блокируется. Recovery остаётся блокирующим до явного успешного callback. `stand.scenario_resource` проверяет conflict, cleanup, timeout/unconfirmed, retry-block и recovery. Legacy plugin `void safe_stop` честно остаётся INDETERMINATE; конкретные plugin confirmations требуют отдельного bench/Evidence gate. |
| Общий desktop, project selection, Free и Admin V1 | `CONTRACT_TESTED`; задачи 07 и 09 закрыты локально | Home перечисляет package workflows; `operator_action` маршрутизируется delivery composition. Free launcher показывает requirements, сохраняет конфигурационный snapshot/hash и разрешает только dynamic workflow. Admin V1 read-only. Published scenario save заблокирован, draft получает отдельную identity. `desktop.test_page_smoke` проверяет эти UI contract seams. |
| Производственные пакеты, повтор узла и актуальный статус изделия | `CONTRACT_TESTED` | KTMA project package задаёт пять зарегистрированных production workflows; ledger сохраняет отдельные попытки и audit trail выбора последнего принятого результата по затронутым компонентам; stand error не заменяет verdict DUT. Contract проверяет FULL → fail → equipment error → accepted node rerun без физического DUT. |
| Environment context | `CONTRACT_TESTED`; задача 10 закрыта локально | Отдельные normal/+/- workflows и descriptor; manual condition требует подтверждения и пишет `ENVIRONMENT` Evidence. Setpoints не угадываются. Chamber plugin отсутствует, его физический путь не подтверждён. |
| Release proof | `PACKAGE_SMOKE_TESTED`; задача 11 остаётся `in-progress` | Release CTest 24/24; установленный package распакован вне build tree и прошёл fake-provider/RunStore smoke. GitHub Actions run `36411212720` зелёный только для базового SHA `5d4e0b74a7d23262ef3f51f1fddd07900a9caefc`. Hardware-impacting delivery Evidence gates ещё не закрыты. |
| Studio / Script API | `DEFINED/TODO` | Tree/IR editor и Script API остаются отдельным будущим объёмом. |

Локальная проверка текущей Release-сборки 28.09.2026: `ctest --test-dir
build/Desktop_Qt_6_8_0_MinGW_64_bit-Release --output-on-failure --timeout 60`
прошла **24/24**. `package_stand_win11.ps1` создал
`MilTechStation-KTMA-2.0.0-local-uncommitted-task-close`; ZIP распакован в
новый `%TEMP%` layout, `smoke_stand_package_win11.ps1` завершился
`INSTALLED_PACKAGE_SMOKE_OK` и сохранил/загрузил fake Free run. Исправлены
test-environment PATH для
`ktma.ubsi.equipment_readiness` и fixture `signal=1` / negative-code для
`ktma.ubsi.scenario_runtime`; это не изменяет методику УБСИ. Текущий статус
удалённый CI run `36411212720` зелёный для исходного SHA `5d4e0b7`; он не
включает текущие незакоммиченные правки. Bench/Evidence gates для физических
delivery paths остаются открыты.

## Согласованная граница этапов (2026-09-25)

- Этап A: один УБСИ проходит отдельный ТУ-run в нормальных условиях через
  MilTechStation с Evidence и отчётом. Это внутренний milestone.
- Этап B: пять производственных пакетов `FULL`, `POWER`, `YALK`, `YTP`, `YVP`,
  минимальная админка и свободный режим. После `FULL` выборочный повтор узла
  создаёт новый run, сохраняет историю и обновляет текущий статус изделия по
  последней принятой проверке этого узла. Финальный ТУ запускается отдельно.
- Гибкость этапа B доказывается без перекомпиляции: новая композиция сценария
  из существующих процедур, смена совместимого resource binding и свободная
  проверка со своим допуском; версии конфигурации сохраняются вместе с run.
- Климатические ТУ `+`/`−` — отдельные запуски при заданных условиях, без
  post-climate workflow в этом объёме. PPB — последующий проект, не критерий B.
- Персональные логины не обязательны для первого выпуска: UI разделяет
  оператора и настройку, оператор фиксируется в run, опубликованная
  конфигурация защищена правами ОС.

Эти решения определяют backlog, но не утверждают, что пути уже реализованы
или стендово подтверждены.

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

## Closure / testability policy

Общая продуктовая шкала закрытия определена в `docs/product/miltechstation.md`:

```text
DEFINED
IMPLEMENTED
CONTRACT_TESTED
INTEGRATION_TESTED
BENCH_VERIFIED       # если feature аппаратно-значим
EVIDENCE_VERIFIED    # если feature влияет на формальный run
CLOSED
```

Правила для backlog этого плана:

- `[x]` у кода означает, что конкретная подзадача сделана, но не обязательно что весь feature `CLOSED`;
- обязательный CI текущего `master` должен быть зелёным для статуса `CLOSED`;
- аппаратный feature не закрывается только mock/CTest;
- feature, меняющий физическое воздействие, freshness, criterion или cleanup, требует нового bench verification;
- Run/Evidence/report feature закрывается только после проверки сохранённого результата, а не только renderer/unit test;
- project/delivery может вводить дополнительный gate, не протаскивая его в generic core.

Для УБСИ действует дополнительный gate:

```text
TU -> master path -> frozen donor trace -> automated test -> live master bench -> Evidence
```

Канонический task contract: `docs/task/ubsi/traceability.md`.

- [x] продуктовая closure шкала зафиксирована;
- [x] UBSI-specific donor trace gate зафиксирован;
- [ ] по мере реализации каждый active slice должен иметь явный текущий closure status;
- [ ] перед переводом milestone в `CLOSED` проверить, что его обязательные child slices закрыты по назначенной policy.

## Этап 1 — Project package contract

Статус: **CONTRACT_TESTED; локальный полный CTest GREEN, remote CI не подтверждён**

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
- [x] Windows CI после project/runtime slice проходил полностью.

Примечание: перечисленные контрактные подзадачи сделаны. Прежний успешный
Windows CI относился к более раннему срезу; до восстановления зелёного
текущего master нельзя сохранять статус `CLOSED` даже для V1-контракта.
Это также не означает готовность всех workflow КТМА.

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

- [x] `MeasurementEvidence` сохраняет resource/device/quality при `PROVIDED`
  либо явный `NOT_PROVIDED`; round-trip проверяется `stand.run_store_context`;
- [ ] связать delivery readiness с общим run/evidence lifecycle;
- [x] `RunArtifacts::attachTo()` сохраняет каталог, relative path, размер и
  SHA-256 telemetry/raw files в `RunStore`; round-trip и HTML link проверяет
  `stand.run_store_context`;
- [ ] integration regression доказать command/ack/error/safety event ordering;
- [x] generic `runScenarioWithEvidence()` → SQLite save/load → HTML re-render
  проверяется `stand.run_store_context`;
- [x] foundation закрыта как `CONTRACT_TESTED`; hardware delivery-specific
  Evidence gates остаются отдельными.

## Этап 3 — Resource ownership / safety

Статус: **PARTIAL / ACTIVE**

Сделано:

- [x] ISD driver поддерживает owner-scoped mutations и `release_owner`;
- [x] generic ISD safe-stop не использует firmware type=4;
- [x] YALK overload использует отдельные ownership domains для background DAC и transient ±12 V impact;
- [x] аварийный cleanup перегрузки адресный и не требует global reset.
- [x] `ScenarioEngine::run()` атомарно захватывает все явно объявленные
  `requiredResources` через `ResourceLeaseManager` до первого физического
  шага и удерживает их до своего безусловного `safeStopAll()`;
- [x] конфликт выдаёт `Incomplete` / `RESOURCE_LEASE` с именем занятого
  ресурса и владельцем, не вызывая процедуру или `safeStopAll()`;
- [x] Main, Universal Free и КТМА передают session-owned lease-manager в
  audited common run entry point;
- [x] `stand.scenario_resource` проверяет conflict, отсутствие оборудования
  при конфликте и release после run.

Остаётся:

- [ ] обеспечить mandatory resource declaration или безопасную policy для
  legacy capability-only сценариев; сейчас без declared resources lease не
  применяется и это явно фиксируется в RunEvent;
- [ ] resource states READY/ACTIVE/SAFE/ERROR/INDETERMINATE;
- [ ] ISD timeout -> indeterminate semantics в общем resource state;
- [ ] operator recovery/restart UX;
- [ ] equipment safety limits metadata;
- [ ] contract/integration tests на conflict, timeout, cleanup и partial failure;
- [ ] для hardware-impacting safety path получить bench/evidence до `CLOSED`.

## Этап 4 — KTMA project composition

Статус: **PARTIAL / ACTIVE**

Сделано:

- [x] build runtime сохраняет `projects/` и package-relative `data/` рядом с приложением;
- [x] `KtmaMainWindow` загружает `ProjectDefinition` из `MILTECH_PROJECT` либо `projects/ktma/project.yaml` до инициализации station runtime;
- [x] профиль стенда выбирается из `ProjectDefinition.equipmentProfilePath`;
- [x] заголовок и log получают identity выбранного project package;
- [x] Free workflow выполняется через `runProjectWorkflow()` и получает project/workflow context;
- [x] Home page перечисляет project workflows и отправляет выбранный workflow ID delivery-owned dispatch; Universal Free launcher принимает workflow ID, отображает requirements и блокирует запуск при отсутствующей profile binding;
- [x] `stand.project_definition` и `desktop.test_page_smoke` проверяют package/workflow/run-context и UI dispatch/resources;
- [x] package script включает `projects/` и package-relative `data/`; установленный runtime smoke из `build/deploy` успешно запущен и закрыт;
- [x] полный Release CTest после UI/package изменения: 24/24;
- [x] Free mode остаётся вне registrar;
- [x] отсутствие package имеет compatibility fallback на старый raw ScenarioEngine path.

Остаётся:

- [ ] общий desktop composition больше не должен наследовать product/project setup от `KtmaMainWindow`;
- [x] текущую физическую readiness оставить delivery-owned; оператор видит обязательные ресурсы и проверяет их на delivery screen перед запуском.

## Этап 5 — УБСИ TU NORMAL

Не менять нормативную методику ради архитектурного refactor.

UBSI slice закрывается только по `docs/task/ubsi/traceability.md`, включая обязательную сверку физического пути с frozen donor `rebuild/tu-minimal-clean@e4ca10616e7d85f8ac9a0535482612a6d4d8f20b`.

### ЯЛК / перегрузка — текущий slice

Статус: **IMPLEMENTED + AUTO_TESTED + DONOR_TRACED / MASTER BENCH REQUIRED / REMOTE CI UNCONFIRMED**

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
- [x] regression `ktma.ubsi.yalk_overload_physical` фиксирует порядок команд, freshness и compatibility для historical `*_count=88`;
- [x] ранее targeted overload slice проходил CI;
- [x] canonical `docs/task/ubsi/testing.md` синхронизирован с безопасной физической последовательностью;
- [x] donor trace/closure policy вынесены в `docs/task/ubsi/traceability.md`.

Остаётся:

- [ ] вернуть текущий master в GREEN: сейчас два regression вокруг YALK open-input semantics блокируют общий closure;
- [ ] сделать explicit safe-address args в canonical scenario вместо compatibility `*_count=88`;
- [ ] синхронизировать `PROJECT_MEMORY.md` после scenario-data migration;
- [ ] выполнить новый полный живой overload run актуального master;
- [ ] проверить Evidence и cleanup этого run;
- [ ] по результату живого прогона отдельно решить timing/criterion, не копируя donor автоматически;
- [ ] выставить окончательный parity `SAME`/`INTENTIONAL_DIFF` и только затем `CLOSED`.

### Остальной TU NORMAL

Для каждого пункта ниже обязательны: master path, donor trace, automated regression, GREEN CI, live master bench, Evidence.

- [ ] equipment readiness end-to-end evidence;
- [ ] power/readiness;
- [ ] ЯЛК initial/open path закрыть после устранения текущих regressions;
- [ ] ЯЛК полный analog/contact/reference тракт;
- [ ] ЯТП;
- [ ] ЯВП commissioning/physical path;
- [ ] TU coverage gate;
- [ ] Evidence + TU report;
- [ ] полный живой прогон;
- [ ] все mandatory строки `docs/task/ubsi/traceability.md` получить `CLOSED` либо явный нормативно обоснованный отдельный status.

## Этап 6 — Production

Статус: **OPEN / PARTIAL COMPONENTS EXIST**

- [ ] registered DUT required через project workflow;
- [ ] scopes/packages;
- [ ] production evidence;
- [ ] final TU reference run;
- [ ] normal-condition production workflows; climate `+`/`−` is a later milestone;
- [ ] contract tests registration policy + immutable scenario identity;
- [ ] integration test production workflow without physical DUT;
- [ ] final production bench run + Evidence before `CLOSED`.

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
- [ ] roles model с выключенным enforcement;
- [ ] editor round-trip tests `UI -> IR -> YAML -> IR`;
- [ ] operator/engineering smoke tests на project screens.

## Этап 8 — Environment

- [ ] environment data model;
- [ ] continuous environment evidence;
- [ ] manual climate flow;
- [ ] controlled chamber capability;
- [ ] separate climate `+` and `−` TU workflows during specified conditions;
- [ ] vibration capability;
- [ ] parallel exposure + monitoring;
- [ ] simulator/integration tests;
- [ ] реальные chamber/vibration paths требуют отдельного `BENCH_VERIFIED`.

## Этап 9 — Script runtime / PPB

- [ ] Station Script API;
- [ ] LuaHost;
- [ ] production script identity/hash/runtime;
- [ ] разобрать PPB на project/scenario/Lua/generator;
- [ ] генератор только через `signal.generator`;
- [ ] migration без отдельного PPB application;
- [ ] architecture-leak regression: PPB не добавляет PPB-specific code в core;
- [ ] Lua sandbox/resource API contract tests;
- [ ] PPB становится последующим project-level proof после production/flexibility stage:
      closed только после собственного end-to-end run.

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

- `deliveries/ktma/ubsi/src/procedures/yalk_initial_physical.cpp`
- `deliveries/ktma/ubsi/src/procedures/yalk_overload_physical.cpp`
- `deliveries/ktma/ubsi/src/procedures/registration_layers.h`
- `deliveries/ktma/ubsi/src/procedures/procedure_registry.cpp`
- `deliveries/ktma/ubsi/tests/yalk_initial_physical_test.cpp`
- `deliveries/ktma/ubsi/tests/yalk_overload_physical_test.cpp`
- `deliveries/ktma/ubsi/CMakeLists.txt`

## Изменённые документы

- `docs/product/miltechstation.md`
- `docs/product/data.md`
- `docs/reference/components/project-package.md`
- `docs/reference/INDEX.md`
- `docs/task/ubsi.md`
- `docs/task/ubsi/testing.md`
- `docs/task/ubsi/traceability.md`
- `plans/active/ubsi-tu-on-miltechstation.md`
- этот план.
