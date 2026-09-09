# VISA / SCPI

## Не путать два понятия

VISA и SCPI решают разные задачи.

```text
VISA
    =
способ найти прибор,
открыть сессию,
отправить/получить байты

SCPI
    =
язык команд прибора
```

Один прибор может использовать SCPI через VISA.

Другой прибор может использовать похожие команды через COM/LAN без VISA.

---

# Текущая VISA-реализация

Код:

```text
stand/adapters/visa_instrument.cpp
```

В текущем проекте adapter работает на Windows.

Runtime динамически загружает:

```text
visa64.dll
```

либо:

```text
visa32.dll
```

из Windows System32.

Используются стандартные функции VISA:

```text
viOpenDefaultRM
viFindRsrc
viOpen
viSetAttribute
viWrite
viRead
viClose
```

---

# Поиск ресурса

Plugin задаёт одно или несколько VISA resource-expression.

Adapter:

```text
expression
    ↓
viFindRsrc
    ↓
первый подходящий resource
    ↓
viOpen
```

Конкретный VISA resource является конфигурацией стенда и не должен быть
зашит в общую архитектуру MilTechStation.

---

# Базовые операции adapter

```text
write(command)
query(command)
queryRaw(command)
```

`query`:

```text
write
  ↓
optional delay
  ↓
read
```

Пустой ответ считается ошибкой транспорта/прибора.

---

# Текущие VISA-компоненты

Через VISA в текущем коде работают:

## В7-78/1

Plugin:

```text
orbita.v7_visa
```

Возможности:

```text
measure.reference_voltage
measure.dc_current
measure.reference_ac_voltage
measure.reference_frequency
```

Конкретные команды измерения задаются конфигурацией плагина.

## Rigol generator

Plugin:

```text
orbita.rigol_generator
```

Capability:

```text
signal.generator
```

Использует SCPI-команды для настройки синусоидального сигнала и выхода.

## Rigol DHO8xx

Plugin:

```text
orbita.rigol_dho8xx
```

Capability:

```text
measure.waveform
```

Использует SCPI для настройки канала и получения waveform.

---

# АКИП

Текущая реализация АКИП-1160/6 в репозитории использует отдельный
serial/COM adapter.

Поэтому нельзя писать:

```text
всё лабораторное оборудование КТМА работает только через VISA
```

Корректнее:

```text
оборудование подключается через плагины,
а конкретный transport зависит от прибора
```

---

# Уровень MilTechStation

MilTechStation не должен требовать VISA или SCPI в своём общем сценарии.

Сценарий работает с capability:

```text
measure.reference_voltage
signal.generator
power.dc_supply
```

А конкретный plugin решает, каким транспортом общаться с прибором.
