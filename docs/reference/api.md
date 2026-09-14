# API

Здесь описаны программные интерфейсы, которыми части системы вызывают
друг друга внутри одного процесса или на уровне C++ библиотеки.

ABI отдельно:

[Equipment Plugin ABI](abi.md)

---

# 1. Публичный API liborbita

Код:

```text
orbita/include/orbita.h
```

Основной класс:

```cpp
orbita::Orbita
```

Контракт заголовка зафиксирован как C++17 API.

## Входные каналы

Клиент передаёт:

```cpp
struct ChannelSpec {
    std::string address;
    std::string name;
    std::string category;
};
```

`address` должен быть нормализован до передачи в ядро.

Ядро не должно заниматься:

- пользовательскими кодировками;
- исправлением UI-ввода;
- SQLite;
- таблицами;
- графиками;
- допусками;
- нормативной оценкой изделия;
- выбором физического АЦП/источника телеметрии.

## Входной поток

Новый основной путь:

```cpp
pushSamples(const std::vector<int16_t>& samples)
```

Источник отсчётов выбирает runtime MilTechStation из профиля поставки и
передаёт полученные порции данных в `liborbita`.

Переходно остаются legacy-методы:

```cpp
setDeviceE2010(channel, rate_khz);
setDeviceNone();
```

Они нужны существующему desktop-коду до завершения миграции. Новый station
code не должен привязывать `liborbita` к E20-10 через эти методы.

## Каналы

```cpp
setChannels(...)
getChannels()
```

Набор каналов можно менять во время работы.

## Жизненный цикл

```cpp
start()
stop()

pause()
resume()

isRunning()
isPaused()
```

## Получение данных

Pull:

```cpp
waitForData(timeout)
getSnapshot()
```

Push:

```cpp
setDataCallback(...)
```

Callback вызывается из декодерного потока и не должен выполнять долгую
блокирующую работу.

## Snapshot

```cpp
struct Snapshot {
    uint32_t mtv_seconds;
    std::vector<ChannelValue> values;
    Stats stats;
};
```

Значение канала:

```cpp
struct ChannelValue {
    std::string address;
    double value;
    bool valid;
};
```

Критически важно:

```text
ChannelValue.value
=
декодированный сырой код канала
```

Это не гарантированная физическая величина в вольтах, омах, градусах и
т.п.

Перевод в физическую величину выполняется более высоким слоем.

## Чтение одного канала

```cpp
getValueByAddress(address)
```

Возвращается:

```cpp
std::optional<double>
```

## Статистика

```cpp
Stats {
    phrase_error_percent
    group_error_percent
    frames_processed
    mb_per_second
}
```

## Запись

```cpp
startRecording(filename)
stopRecording()
isRecording()
```

---

# 2. Component Profile MilTechStation

Поставка декларирует состав станции в `StandProfile`.

Общая модель:

```cpp
struct ComponentProfile {
    std::string id;
    std::string kind;
    std::string provider;
    bool enabled;
    std::vector<std::string> bindings;
    std::map<std::string, std::string> configuration;
    std::vector<std::string> capabilities;
};
```

`kind` задаёт тип station-level компонента, `provider` — конкретную
реализацию, а `bindings` — логические роли, по которым остальные части станции
находят компонент.

Для `kind=equipment` поле `capabilities` отдельно описывает контракты
оборудования. Роль и capability — разные измерения:

```text
power.dut             = роль в конкретной поставке
power.dc_supply       = контракт оборудования
orbita.akip_1160_pair = provider конкретной реализации
```

Поэтому две поставки могут назначить разные приборы роли `power.dut`, а одна
поставка может иметь `power.dut` и `power.aux` с одной capability
`power.dc_supply`.

Пример:

```yaml
components:
  - id: dc-supply
    kind: equipment
    provider: orbita.akip_1160_pair
    bind:
      - power.dut
    capabilities:
      - power.dc_supply
```

Legacy-профили, где у equipment нет `capabilities:`, всё ещё поддерживаются:
в них `bind:` трактуется как старый список capability. Такой формат является
compatibility input и не должен использоваться для новых поставок.

Сейчас реализованы следующие component kinds:

```text
equipment
sample_source
execution_runtime
```

Модель специально не ограничена enum. Появление Python, Lua, replay source,
SSH transport или другой составной части не должно требовать изменения базовой
схемы профиля.

Пример декларации входа телеметрии КТМА:

```yaml
components:
  - id: orbita-sample-input
    kind: sample_source
    provider: miltech.sample.e2010
    bind:
      - telemetry.orbita.sample_source
```

Для чтения декларации без инстанцирования используются:

```cpp
findComponentById(...)
findComponentByBinding(...)
```

---

# 3. Component Runtime MilTechStation

Код:

```text
station/include/orbita_stand/component_runtime.h
```

`ComponentRuntime` — общий lifecycle-контейнер station-level компонентов.
Он не знает о конкретных E20, Python, Lua или SSH. Runtime знает только `kind`,
для которого зарегистрирована фабрика, и передаёт ей соответствующий
`ComponentProfile`.

Базовая граница:

```cpp
class IStationComponent {
public:
    virtual std::string_view componentKind() const noexcept = 0;
    virtual void safeStop() noexcept = 0;
};
```

Основные операции runtime:

```cpp
registerKindFactory(kind, factory)
instantiate(profile)
instantiate(profile, selectedKinds)
findById(id)
findByBinding(binding)
safeStopAll()
clear()
```

`instantiate(profile, selectedKinds)` используется для поэтапной миграции:
например, можно перевести `sample_source` на общий runtime, пока старое
`equipment` всё ещё работает через `EquipmentRegistry`.

Один logical binding не может одновременно принадлежать двум активным
компонентам одного runtime. Такая конфигурация отвергается как неоднозначная.

`clear()` и разрушение runtime выполняют best-effort `safeStop()` для уже
созданных компонентов.

---

# 4. Sample Source API MilTechStation

Код:

```text
station/include/orbita_stand/sample_source.h
```

`ISampleSource` является `IStationComponent` вида `sample_source` и владеет
жизненным циклом физического источника сырых отсчётов:

```text
open / close
start / stop
samples callback
error callback
```

Первый provider:

```text
miltech.sample.e2010
```

Он реализован на уровне station adapters. `liborbita` знает только о входном
потоке `int16` и не должна включать E20-10 в свою продуктовую модель.

Регистрация component kind:

```cpp
registerSampleSourceComponents(componentRuntime)
```

`orbita_telemetry_probe` уже использует именно этот путь: загружает профиль
поставки, находит `telemetry.orbita.sample_source` и подаёт отсчёты через
`Orbita::pushSamples()`.

---

# 5. Execution Runtime MilTechStation

Код:

```text
station/include/orbita_stand/execution_runtime.h
```

Этот контракт нужен для существующего стендового ПО, которое разумнее сначала
запускать как готовую программу/скрипт, а не переписывать под встроенный
ScenarioEngine.

Общий запрос:

```cpp
struct ExecutionRequest {
    std::string target;
    std::vector<std::string> arguments;
    std::map<std::string, std::string> environment;
    std::string workingDirectory;
    int timeoutMs;
};
```

Результат:

```cpp
struct ExecutionResult {
    int exitCode;
    bool started;
    bool cancelled;
    bool timedOut;
    std::string standardOutput;
    std::string standardError;
};
```

`IExecutionRuntime` является station component вида:

```text
execution_runtime
```

и предоставляет:

```cpp
execute(request)
cancel()
isRunning()
```

Первый provider:

```text
miltech.exec.process
```

Он использует отдельный процесс, поддерживает рабочий каталог, environment,
аргументы, stdout/stderr, timeout и остановку. Это базовый путь для подключения
существующего Python-приложения, интерпретатора со скриптом либо автономной
стендовой программы без встраивания её логики в MilTechStation.

Конфигурация provider поддерживает:

```text
program
entrypoint
working_directory
timeout_ms
start_timeout_ms
terminate_grace_ms
```

Если `program` задан, `target` запуска передаётся первым аргументом программе.
Это позволяет декларативно задать интерпретатор, а конкретный скрипт передавать
через request. Если `program` не задан, `target` является самим исполняемым
файлом.

Регистрация component kind:

```cpp
registerExecutionRuntimeComponents(componentRuntime)
```

Embedded Lua/Python providers пока не реализованы. Они должны реализовать тот
же lifecycle/result contract, а не вводить отдельный способ управления run.

---

# 6. Equipment resources MilTechStation

`EquipmentRegistry` поддерживает два пути одновременно.

Legacy/default routing:

```text
capability
    ↓
operation
    ↓
device
```

Resource-aware routing:

```text
logical resource role
    ↓
capability
    ↓
operation
    ↓
конкретный component instance
```

Код:

```text
station/include/orbita_stand/equipment_runtime.h
```

Legacy API:

```cpp
hasCapability(capability)
invoke(capability, operation, arguments)
```

Resource API:

```cpp
resourceHasCapability(resource, capability)
invokeResource(resource, capability, operation, arguments)
resources()
```

Поставка автоматически регистрирует concrete component id как resource id. Для
канонического equipment-профиля все значения `bind:` также регистрируются как
логические resource roles.

Например:

```text
resource   = power.dut
capability = power.dc_supply
operation  = set_voltage
arguments  = volts=27
```

Это позволяет иметь несколько независимых устройств с одной capability без
перезаписи друг друга в registry.

Capability-only routing временно сохраняется для существующих сценариев и
UI. После миграции сценариев неоднозначный default routing должен быть удалён
или запрещён.

---

# 7. Resource requirements ScenarioEngine

`ScenarioNode` может требовать конкретный logical resource вместе с
capability:

```cpp
struct ResourceRequirement {
    std::string resource;
    std::string capability;
};
```

YAML:

```yaml
resources:
  - resource: power.dut
    capability: power.dc_supply
```

До вызова процедуры `ScenarioEngine` проверяет
`resourceHasCapability(resource, capability)`. Если конкретный ресурс не
доступен, шаг получает `INCOMPLETE`; процедура не запускается.

Внутри процедуры resource-aware вызов выполняется через:

```cpp
context.equipment.invokeResource(
    resource,
    capability,
    operation,
    arguments);
```

Старый `requires:` с capability-only проверкой пока остаётся совместимым
параллельным механизмом.

---

# 8. Аргументы Equipment API

На C++ уровне аргументы представлены:

```cpp
std::map<std::string, std::string>
```

Через Equipment Plugin ABI они сериализуются в простой текстовый формат.

Формат описан в:

[Equipment Plugin ABI](abi.md)

---

# 9. Граница API и требований

API отвечает:

> Что программа умеет вызвать?

ТУ отвечает:

> Что необходимо проверить?

Наличие операции в API не означает, что она должна использоваться в
конкретном испытании УБСИ.

Аналогично существование плагина или component provider не делает прибор
обязательным для конкретного сценария.
