# Universal MilTechStation — backend refactor

## Цель

Сделать MilTechStation общей платформой автоматизированного стенда, а КТМА / УБСИ — одной из поставок поверх неё.

Ключевые инварианты:

- состав реального стенда задаёт `StandProfile.components`, а не код общего ядра;
- `role/resource`, `capability` и `provider` — разные сущности;
- protocol consumer не выбирает и не владеет физическим источником данных;
- существующее стендовое ПО сначала подключается как готовый процесс/скрипт, без обязательного переписывания;
- физические последовательности включения, readiness и safe-stop не должны теряться при рефакторинге;
- неподтверждённая физическая карта/коммутация не угадывается: такой путь завершается `INCOMPLETE` до опасного воздействия.

Базовая композиция:

```text
delivery profile
    ↓
StationSession
    ├─ ComponentRuntime
    │    ├─ sample_source
    │    └─ execution_runtime
    └─ Equipment runtime
         ↓
resource role → capability → provider/device
```

Для протокольного потока:

```text
sample_source provider
    ↓ raw samples
integration boundary
    ↓
protocol consumer (например liborbita)
    ↓ decoded values
```

`liborbita` не выбирает E20-10 и не зависит от Lusbapi. E20-10 — station-level provider поставки.

## 1. Декларативный состав станции

Статус: **DONE**

- [x] `ComponentProfile` и `StandProfile.components`;
- [x] `kind / provider / bind / capabilities / config`;
- [x] KTMA equipment и E20-10 заданы через `components:`;
- [x] legacy `devices:` поддерживается только как compatibility input/view;
- [x] двойная декларация одного component id запрещена;
- [x] contract tests проверяют реальный KTMA profile.

## 2. Общий lifecycle компонентов

Статус: **DONE / FIRST VERSION**

- [x] `IStationComponent`;
- [x] `ComponentRuntime` и kind factories;
- [x] поиск по id/binding;
- [x] selective instantiation kinds;
- [x] запрет неоднозначных bindings;
- [x] rollback и best-effort safe-stop;
- [x] contract tests.

## 3. Источники сырых отсчётов

Статус: **DONE**

- [x] station-level `ISampleSource`;
- [x] `miltech.sample.e2010`;
- [x] `liborbita::pushSamples()` как единственный station-facing raw input;
- [x] `orbita_telemetry_probe` и desktop получают `telemetry.orbita.sample_source` из station component model;
- [x] `OrbitaSampleBridge` вынесен в `integrations/orbita`;
- [x] desktop запускает/останавливает поток через `OrbitaSampleBridge`, а не ручные callbacks;
- [x] legacy `setDeviceE2010()/setDeviceNone()` удалены;
- [x] старые `orbita/device/e2010_device.*` и liborbita-level `ISampleSource` удалены;
- [x] target `orbita` не включает и не линкует Lusbapi; Lusbapi принадлежит E20 station provider.

## 4. Запуск существующего стендового ПО

Статус: **FIRST VERSION**

- [x] `IExecutionRuntime`;
- [x] `ExecutionRequest / ExecutionResult`;
- [x] `miltech.exec.process` через `QProcess`;
- [x] args / env / cwd / stdout / stderr;
- [x] timeout / cancel / terminate / kill fallback;
- [x] интеграционный тест запускает реальный дочерний процесс;
- [ ] связать execution runtime с общим run/evidence lifecycle;
- [ ] подключить первый реальный Python-стенд через process provider;
- [ ] подключить PPB/Lua через тот же run contract;
- [ ] embedded Python/Lua вводить только если external process недостаточен.

## 5. Границы домена

Статус: **PARTIAL / ACTIVE MIGRATION**

Сделано:

- [x] `orbita_stand_runtime` не зависит от UBSI procedure domain/reporting;
- [x] station adapters не получают UBSI domain транзитивно;
- [x] общие typed equipment contracts вынесены отдельно;
- [x] compiled YALK/UBSI procedure implementations перенесены из `station/src` в `deliveries/ktma/ubsi/src/procedures`;
- [x] KTMA procedure implementation собирается отдельным `ktma_ubsi_procedures` target;
- [x] Orbita telemetry procedures принадлежат `integrations/orbita`, а не generic station target;
- [x] generic `station` CMake больше не содержит product procedure domain target;
- [x] KTMA procedure/runtime contract tests собираются со стороны delivery.

Остаётся:

- [ ] перенести compatibility headers `ubsi_procedures.h`, `yalk_analog_procedure.h`, `ubsi_yvp_math.h` из generic station include в delivery-owned include API;
- [ ] проверить и удалить/перенести оставшиеся неиспользуемые legacy `ubsi_procedures_entry.cpp` / `ubsi_procedures_yvp_v7.cpp`;
- [ ] разнести KTMA-specific ROKT/ULK/ISD implementations и действительно общие station adapters.

## 6. Resource / role model

Статус: **PARTIAL / ACTIVE MIGRATION**

Канонический вызов:

```text
logical delivery role
    ↓
component instance
    ↓
capability
    ↓
operation
```

Реализовано:

- [x] `bindResource`, `resourceHasCapability`, `invokeResource`, introspection;
- [x] несколько resources могут иметь одинаковую capability;
- [x] `ComponentProfile.bind` отделён от equipment `capabilities`;
- [x] KTMA profile задаёт роли `power.dut`, `dut.parameter_source`, `switch_matrix.primary`, `measure.reference`, `signal.primary`, `measure.waveform.primary`;
- [x] `ScenarioNode.requiredResources` и YAML `resources:`;
- [x] preflight resource+capability до запуска procedure;
- [x] compatibility router направляет старый `invoke(capability)` через единственный resource конкретного leaf node;
- [x] неоднозначный leaf с двумя resources одной capability требует `invokeResource()`;
- [x] production scenarios используют delivery resources;
- [x] canonical published full TU `ubsi_ulk_combined_check.yaml` использует delivery resources для физического оборудования;
- [x] standalone published YALK TU использует delivery resources;
- [x] desktop readiness после probe восстанавливает canonical resource aliases на тех же проверенных device instances;
- [x] contract test сверяет scenario resource contracts с реальным `stand_ktma.yaml`.

Временно физические `requires:` в мигрированных сценариях сохраняются как compatibility preflight старого desktop. Фактические legacy procedure invokes при наличии `resources:` уже маршрутизируются через выбранную роль.

Остаётся:

- [ ] перевести standalone YTP и оставшиеся KTMA/TU scenarios;
- [ ] перед этим перевести их runtime tests/fakes на resource-aware provider;
- [ ] после полной миграции убрать физические capability-only `requires:` и запретить неоднозначный global default routing.

## 7. Serial / SSH / board debugging

Статус: **TODO**

Не делать одну искусственную универсальную «команду платы». После появления первого реального consumer нужны отдельные station contracts:

- serial/raw console;
- SSH command/session/file transfer;
- специализированный debug transport — только если реально потребуется.

COM/baud/IP/credentials принадлежат delivery/environment, а не station core.

## 8. Desktop / application composition

Статус: **PARTIAL**

- [x] `StationSession` владеет profile, `ComponentRuntime`, EquipmentPluginManager/Registry и device retention;
- [x] `StationSession` поддерживает `Immediate` и `Deferred` equipment composition;
- [x] Deferred mode загружает providers и non-equipment components, но не захватывает физические devices до readiness;
- [x] contract test проверяет, что Deferred не создаёт equipment, а Immediate сохраняет прежнюю семантику;
- [x] desktop ownership переключён на `StationSession(Deferred)`; старые desktop поля являются non-owning compatibility views;
- [x] desktop E20/sample source находится в station component model;
- [x] readiness resource aliases восстанавливаются на проверенных экземплярах оборудования без создания скрытого второго device;
- [x] `OrbitaSampleBridge` используется как desktop integration boundary;
- [x] KTMA readiness sequence питание УБСИ → выдержка → проверка адаптера сохранена;
- [ ] убрать оставшийся equipment/readiness orchestration из `MainWindow` в application-neutral/delivery lifecycle;
- [ ] product/delivery package подключать композицией, а не расширением `integration*()` API;
- [ ] операторский UX УБСИ не перерабатывать в рамках backend migration.

## Проверка

Ветка `universal_miltechstation` имеет отдельный GitHub Actions workflow:

```text
.github/workflows/universal-miltechstation-ci.yml
```

Он выполняет Windows UCRT64 / Qt 6 / GCC configure, build и CTest. Результат считается подтверждённым только после завершения соответствующего workflow run со статусом `success`.

## Следующие шаги

1. держать текущий backend-срез зелёным по CI;
2. завершить перенос compatibility headers и аудит legacy UBSI procedure sources;
3. вынести readiness orchestration из `MainWindow`, сохранив физический порядок КТМА;
4. закончить resource migration standalone YTP и остальных TU-сценариев;
5. связать `execution_runtime` с run/evidence и подключить первый существующий Python/Lua стенд;
6. после появления реального board consumer добавить serial/SSH transport contracts.
