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

`liborbita` не должна выбирать E20-10 и не должна зависеть от Lusbapi после
завершения миграции desktop.

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

Статус: **PARTIAL**

- [x] общий `ISampleSource` на уровне station;
- [x] E20-10 реализован как station provider `miltech.sample.e2010`;
- [x] `liborbita::pushSamples()` добавлен как независимый вход;
- [x] `sample_source` подключён к `ComponentRuntime`;
- [x] `E2010SampleSource::stop()` освобождает I/O events даже после аварийного
  завершения acquisition thread;
- [x] `orbita_telemetry_probe` больше не создаёт E20 через `liborbita`: probe
  загружает delivery profile, берёт `telemetry.orbita.sample_source` из
  `ComponentRuntime` и подаёт raw samples в `Orbita::pushSamples()`;
- [ ] desktop должен брать `telemetry.orbita.sample_source` из runtime;
- [ ] удалить legacy `setDeviceE2010()/setDeviceNone()` из liborbita;
- [ ] удалить `orbita/device/e2010_device.*` и старый `ISampleSource`;
- [ ] удалить зависимость liborbita от Lusbapi.

### 4. Запуск существующего стендового ПО

Статус: **FIRST VERSION**

- [x] общий `IExecutionRuntime`;
- [x] `ExecutionRequest` / `ExecutionResult`;
- [x] provider `miltech.exec.process` через `QProcess`;
- [x] working directory / env / args / stdout / stderr;
- [x] timeout / cancel / terminate / kill fallback;
- [x] factory contract test;
- [ ] интеграционный тест реального запуска процесса;
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
- [x] desktop теперь явно объявляет зависимость от текущего KTMA/UBSI domain,
  вместо получения её через generic runtime;
- [x] UBSI production scenario test явно линкует текущий procedure domain,
  поэтому generic runtime не требуется снова загрязнять KTMA-процедурами.

Остаётся:

- [ ] убрать `ubsi_*` и `yalk_*` из самого `orbita_stand_domain` target;
- [ ] перенести KTMA/UBSI procedures в delivery/object package;
- [ ] определить место общих процедур протокола «Орбита» вне core станции;
- [ ] разнести KTMA-specific ROKT/ULK/ISD implementations и действительно
  универсальные station adapters по отдельным targets/packages.

### 6. Resource/role model оборудования

Статус: **PARTIAL / CONTRACT READY**

Канонический профиль описывает component instance отдельно от provider.
`EquipmentRegistry` и ScenarioEngine теперь имеют отдельный role/resource path:

```text
resource id
    ↓
конкретный equipment endpoint
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
- [x] generic `instantiateProfile()` автоматически регистрирует equipment
  component id как resource id и параллельно сохраняет legacy capability route;
- [x] `ScenarioNode` поддерживает `requiredResources` как пары
  `resource + capability`;
- [x] YAML schema шага принимает `resources:` с явными `resource` и
  `capability`;
- [x] ScenarioEngine проверяет resource/capability до запуска процедуры и
  возвращает `INCOMPLETE`, если конкретный ресурс недоступен;
- [x] procedure получает resource-aware вызов через
  `ICapabilityProvider::invokeResource()`;
- [x] отдельный contract test проверяет YAML parsing, missing-resource preflight
  и вызов конкретного logical resource.

Остаётся:

- [ ] перевести существующие KTMA-сценарии постепенно, сохраняя legacy
  capability-only путь на время миграции;
- [ ] определить стабильные delivery role-id там, где component instance id не
  должен становиться публичным именем сценария;
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

Статус: **TODO**

После backend boundaries:

- [ ] убрать lifecycle оборудования из `MainWindow`;
- [ ] вынести station/application session;
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
2. постепенно перевести KTMA scenario YAML на explicit resource + capability;
3. переключить desktop Orbita monitoring на station-owned `sample_source`;
4. после этого физически удалить E20/Lusbapi из `liborbita`;
5. затем физически вынести UBSI/KTMA domain из общего `station` target.
