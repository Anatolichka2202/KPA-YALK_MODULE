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
- нормативной оценкой изделия.

## Источник данных

Основные варианты:

```cpp
setDeviceE2010(channel, rate_khz);
setDeviceNone();
```

`setDeviceNone()` является безопасным режимом без физического источника.

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

# 2. Внутренний Equipment API MilTechStation

Этот API используется сценарием и runtime станции.

Основная идея:

```text
сценарий
   ↓
capability
   ↓
operation
   ↓
конкретное устройство
```

Сценарий не обязан знать имя DLL.

Он спрашивает возможность:

```text
power.dc_supply
measure.reference_voltage
stand.switch_matrix
ulk.parameter_source
signal.generator
...
```

## EquipmentRegistry

Код:

```text
stand/include/orbita_stand/equipment_runtime.h
```

Основные операции:

```cpp
hasCapability(capability)
invoke(capability, operation, arguments)
safeStopAll()
```

Пример концептуально:

```text
capability = power.dc_supply
operation  = set_voltage
arguments  = volts=27
```

Registry находит устройство, которому назначена эта capability, и
передаёт вызов ему.

## Binding

Capability может быть связана:

1. с DLL-плагином;
2. со встроенным источником приложения.

Это позволяет использовать один сценарный контракт независимо от того,
находится реализация внутри процесса или во внешней библиотеке.

## Аргументы

На C++ уровне аргументы представлены:

```cpp
std::map<std::string, std::string>
```

Через ABI они сериализуются в простой текстовый формат.

Формат описан в:

[Equipment Plugin ABI](abi.md)

---

# 3. Граница API и требований

API отвечает:

> Что программа умеет вызвать?

ТУ отвечает:

> Что необходимо проверить?

Наличие операции в API не означает, что она должна использоваться в
конкретном испытании УБСИ.

Аналогично существование плагина не делает прибор обязательным для
конкретного сценария.
