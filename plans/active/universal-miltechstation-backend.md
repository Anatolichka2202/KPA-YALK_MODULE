# Universal MilTechStation — backend refactor

## Цель

Отделить общую платформу MilTechStation от оборудования и логики конкретной
поставки. Состав станции должен задаваться профилем поставки. Протокольные
библиотеки, включая `liborbita`, не должны выбирать или владеть конкретным
физическим оборудованием станции.

Платформа должна в дальнейшем позволять подключать:

- физическое измерительное и управляющее оборудование;
- разные источники сырых потоков данных;
- существующие программы стендов как отдельные процессы;
- Lua/Python runtime;
- serial/SSH и другие средства взаимодействия с вычислительными платами.

## Зафиксированная архитектурная граница

```text
поставка
  ↓
StandProfile / components
  ↓
ComponentRuntime
  ├─ equipment
  ├─ sample_source
  └─ execution_runtime
       ↓
конкретные providers
```

Протокольный consumer находится ниже только по потоку данных:

```text
sample_source provider
  ↓ raw samples
liborbita
  ↓ decoded Orbita values
```

`liborbita` не выбирает E20-10 и не зависит от Lusbapi. E20-10 является
station-level provider конкретной поставки.

## Этапы

### 1. Декларативный состав станции

Статус: **DONE**

- [x] `ComponentProfile` добавлен в `StandProfile`;
- [x] секция `components:` загружается из профиля;
- [x] KTMA декларирует E20-10 как `kind=sample_source`, provider
  `miltech.sample.e2010`;
- [x] KTMA переведён на единую декларацию оборудования через
  `components:` / `kind=equipment`;
- [x] `devices:` больше не является вторым источником истины для KTMA;
- [x] legacy `devices:` остаётся входным форматом для старых поставок;
- [x] для существующего desktop загрузчик автоматически строит
  `StandProfile::devices` из canonical equipment components;
- [x] двойная декларация одного id одновременно в `components:` и `devices:`
  отвергается;
- [x] contract test проверяет реальный профиль КТМА и compatibility view.

### 2. Общий lifecycle компонентов

Статус: **DONE / FIRST VERSION**

- [x] `IStationComponent`;
- [x] `ComponentRuntime`;
- [x] kind factories;
- [x] выбор компонентов по id/binding;
- [x] выборочная инстанциация kinds для постепенной миграции;
- [x] запрет неоднозначных bindings;
- [x] best-effort `safeStopAll()`;
- [x] rollback при ошибке инстанциации;
- [x] contract tests.

### 3. Источники сырых отсчётов

Статус: **DONE**

- [x] общий `ISampleSource` на уровне station;
- [x] E20-10 реализован как station provider `miltech.sample.e2010`;
- [x] `liborbita::pushSamples()` является независимым входом raw samples;
- [x] `sample_source` подключён к `ComponentRuntime`;
- [x] `E2010SampleSource::stop()` освобождает I/O events даже после аварийного
  завершения acquisition thread;
- [x] `orbita_telemetry_probe` загружает delivery profile, берёт
  `telemetry.orbita.sample_source` из `ComponentRuntime` и подаёт raw samples в
  `Orbita::pushSamples()`;
- [x] desktop берёт `telemetry.orbita.sample_source` из station runtime и не
  конфигурирует E20 через `liborbita`;
- [x] удалены legacy `setDeviceE2010()/setDeviceNone()` из публичного API и
  реализации liborbita;
- [x] удалены `orbita/device/e2010_device.*` и старый liborbita-level
  `ISampleSource`;
- [x] target `orbita` больше не включает и не линкует Lusbapi; зависимость
  остаётся только у station provider E20.

### 4. Запуск существующего стендового ПО

Статус: **FIRST VERSION**

- [x] общий `IExecutionRuntime`;
- [x] `ExecutionRequest` / `ExecutionResult`;
- [x] provider `miltech.exec.process` через `QProcess`;
- [x] working directory / env / args / stdout / stderr;
- [x] timeout / cancel / terminate / kill fallback;
- [x] factory contract test;
- [x] интеграционный test запускает реальный дочерний процесс и проверяет
  exit code, stdout/stderr и возврат runtime в idle;
- [ ] связать execution runtime с run lifecycle/evidence;
- [ ] добавить Python provider/профиль после подключения первого реального Python-стенда;
- [ ] добавить Lua provider/adapter при миграции PPB;
- [ ] embedded runtime добавлять только если external-process integration окажется недостаточной.

### 5. Границы домена

Статус: **PARTIAL**

Сделано:

- [x] универсальный `orbita_stand_runtime` больше не линкует
  `orbita_stand_domain` и `orbita_stand_reporting`;
- [x] station IO/adapters больше не получают UBSI domain транзитивно;
- [x] общие typed-контракты оборудования (`IIsdRouter`, `IVoltageSource`,
  `IReferenceVoltmeter`, `IProcedureWaiter`) вынесены из YALK-заголовка в
  `equipment_contracts.h`;
- [x] desktop явно объявляет зависимость от текущего KTMA/UBSI domain,
  вместо получения её через generic runtime;
- [x] UBSI production scenario test явно линкует текущий procedure domain,
  поэтому generic runtime не загрязнён KTMA-процедурами.

Остаётся:

- [ ] убрать `ubsi_*` и `yalk_*` из самого `orbita_stand_domain` target;
- [ ] перенести KTMA/UBSI procedures в delivery/object package;
- [ ] определить место общих процедур протокола «Орбита» вне core станции;
- [ ] разнести KTMA-specific ROKT/ULK/ISD implementations и действительно
  универсальные station adapters по отдельным targets/packages.

### 6. Resource/role model оборудования

Статус: **PARTIAL / FIRST DELIVERY SLICE MIGRATED**

Канонический профиль описывает component instance отдельно от provider.
`EquipmentRegistry` и ScenarioEngine имеют отдельный role/resource path:

```text
logical resource role
    ↓
component instance
    ↓
capability
    ↓
operation
```

Сделано:

- [x] `bindResource(resourceId, ...)` для plugin device и built-in endpoint;
- [x] несколько resources могут иметь одинаковую capability без перезаписи;
- [x] `resourceHasCapability()` валидирует контракт выбранного ресурса;
- [x] `invokeResource()` адресует конкретную роль и capability отдельно;
- [x] `resources()` даёт introspection для UI/runtime;
- [x] `safeStopAll()` учитывает role-bound plugin devices без двойного stop;
- [x] contract test доказывает два независимых источника `power.dc_supply`;
- [x] `ComponentProfile` разделяет `bind` (delivery roles) и
  `capabilities` (equipment contracts); старый equipment-формат без
  `capabilities:` остаётся compatibility input;
- [x] KTMA profile объявляет стабильные роли `power.dut`,
  `dut.parameter_source`, `switch_matrix.primary`, `measure.reference`,
  `signal.primary`, `measure.waveform.primary` отдельно от plugin capabilities;
- [x] generic `instantiateProfile()` регистрирует concrete component id и все
  canonical delivery roles как resources, параллельно сохраняя legacy
  capability-only route;
- [x] `ScenarioNode` поддерживает `requiredResources` как пары
  `resource + capability`;
- [x] YAML schema шага принимает `resources:` с явными `resource` и
  `capability`;
- [x] ScenarioEngine проверяет resource/capability до запуска процедуры и
  возвращает `INCOMPLETE`, если конкретный ресурс недоступен;
- [x] procedure может вызывать конкретный ресурс через
  `ICapabilityProvider::invokeResource()`;
- [x] для миграции старых procedures ScenarioEngine создаёт на каждый leaf
  node resource-router: обычный `invoke(capability, ...)` автоматически идёт
  через единственный resource, объявленный этим шагом;
- [x] если один leaf объявляет несколько resources с одной capability,
  capability-only invoke считается неоднозначным и требует явного
  `invokeResource()`;
- [x] `ktma.ubsi.production.power` переведён на `power.dut` и
  `dut.parameter_source`; старые `ubsi.supply_range`/`ubsi.power_safe_off`
  используют выбранные роли без переписывания процедуры;
- [x] `ktma.ubsi.production.full` переведён на delivery resources для
  физических приборов;
- [x] contract tests проверяют YAML parsing, missing-resource preflight,
  transparent routing старого procedure invoke и роли production сценариев;
- [x] component-profile contract test проверяет, что role не протекает в
  legacy capability view и что explicit capability сохраняется отдельно.

Остаётся:

- [ ] перевести остальные KTMA/TU-сценарии на explicit resource + capability;
- [ ] procedures, которым действительно нужны два ресурса одинаковой
  capability, перевести на явный `invokeResource()`;
- [ ] после миграции запретить неоднозначный default routing по capability.

Это нужно завершить до переноса PPB и других стендов, где одинаковые типы
приборов могут использоваться в разных назначениях.

### 7. Serial / SSH / board debugging

Статус: **TODO**

Не вводить одну искусственную универсальную «команду платы». Нужны отдельные
station-level contracts для транспорта/remote session, когда появится первый
реальный consumer:

- serial/raw console;
- SSH command/session/file transfer;
- при необходимости специализированный debug transport.

Физические параметры и credentials принадлежат профилю поставки/окружению, а
не общему продукту.

### 8. Desktop / application composition

Статус: **PARTIAL**

- [x] добавлен application-neutral `StationSession`, который владеет профилем,
  `ComponentRuntime`, EquipmentPluginManager/Registry и общим safe-stop;
- [x] contract test проверяет configure/clear и автоматический выбор
  non-equipment component kinds из профиля;
- [x] desktop больше не владеет E20 через `liborbita`; sample source создаётся
  из station component profile;
- [ ] переключить desktop с промежуточной ручной композиции на `StationSession`;
- [ ] использовать узкий integration boundary для связи sample source ↔
  protocol consumer вместо разрастания wiring в `MainWindow`;
- [ ] убрать lifecycle оборудования из `MainWindow`;
- [ ] product/delivery package подключать композицией, а не разрастанием
  `integration*()` protected API;
- [ ] сохранить существующую операторскую модель УБСИ без нового redesign.

## Проверка

Для ветки создан отдельный GitHub Actions workflow:

```text
.github/workflows/universal-miltechstation-ci.yml
```

Он собирает проект на Windows UCRT64 / Qt 6 / GCC и запускает CTest при каждом
push в `universal_miltechstation`.

Текущий результат CI фиксировать здесь только после фактического завершения
workflow.

## Текущий следующий шаг

1. держать ветку зелёной через отдельный CI после каждого backend-среза;
2. завершить application composition: desktop → `StationSession`, а связь
   station sample source ↔ liborbita держать в integration boundary;
3. физически вынести UBSI/YALK/KTMA procedures из generic `station` target;
4. параллельно перевести оставшиеся KTMA/TU scenario YAML на delivery roles;
5. связать `execution_runtime` с run/evidence, после чего подключать первый
   реальный Python/Lua стенд без переписывания его логики;
6. затем добавить transport/session contracts для serial/SSH/debug board flows.
