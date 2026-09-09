# Equipment Plugins

## Назначение

Плагины отделяют сценарии MilTechStation от конкретных моделей приборов.

```text
scenario
    ↓
capability
    ↓
Equipment Registry
    ↓
plugin
    ↓
device
```

ABI:

[../abi.md](../abi.md)

---

# Текущие плагины

| plugin_id | Capability | Назначение | Transport |
|---|---|---|---|
| `orbita.ktma_adapter_udp` | `ulk.parameter_source` | адаптер КТМА / ЯЛК / ЯТП | UDP / ROKT |
| `orbita.isd_http` | `stand.switch_matrix` | ИСД | HTTP |
| `orbita.akip_1160_pair` | `power.dc_supply` | питание УБСИ | serial / COM |
| `orbita.v7_visa` | `measure.reference_voltage`, `measure.dc_current`, `measure.reference_ac_voltage`, `measure.reference_frequency` | В7-78/1 | VISA |
| `orbita.rigol_generator` | `signal.generator` | генератор Rigol | VISA / SCPI |
| `orbita.rigol_dho8xx` | `measure.waveform` | осциллограф Rigol | VISA / SCPI |
| `orbita.r4831` | `signal.resistance` | adapter Р4831 | serial |

Наличие плагина в коде не означает, что его функция разрешена в текущей
методике УБСИ.

---

# orbita.ktma_adapter_udp

Capability:

```text
ulk.parameter_source
```

Основная роль:

- запуск подтверждённых ROKT-последовательностей;
- приём UDP;
- выдача ЯЛК;
- выдача ЯТП;
- контроль свежести кадров;
- статистика;
- raw recording.

Protocol:

[../protocols/rokt.md](../protocols/rokt.md)

---

# orbita.isd_http

Capability:

```text
stand.switch_matrix
```

Основные операции текущего API включают:

```text
probe
reset
full_reset
yalk_prepare
yalk_set_voltage
yalk_output_off
switch
analog
```

Активные воздействия блокируются, если профиль или конфигурация не
подтверждают разрешение active outputs.

Protocol:

[../protocols/isd-http.md](../protocols/isd-http.md)

---

# orbita.akip_1160_pair

Capability:

```text
power.dc_supply
```

Основные операции:

```text
probe
read_state
set_voltage
set_current_limit
output
```

Плагин проверяет обратным чтением важные уставки.

При ошибке активной операции старается отключить выходы.

`safe_stop` отключает выходы питания.

Важно:

```text
current limit
!=
критерий потребления изделия
```

---

# orbita.v7_visa

Capabilities:

```text
measure.reference_voltage
measure.dc_current
measure.reference_ac_voltage
measure.reference_frequency
```

Операции:

```text
probe
read_voltage
read_current
read_ac_voltage
read_frequency
```

В текущем УБСИ В7 используется как независимый измерительный эталон там,
где это предусмотрено методикой.

---

# orbita.rigol_generator

Capability:

```text
signal.generator
```

Операции:

```text
probe
set_sine
output
```

Активный output разрешается только при явном подтверждении active outputs.

`safe_stop` пытается отключить оба выхода генератора.

Для ЯВП нельзя включать генератор до подтверждения физической коммутации.

---

# orbita.rigol_dho8xx

Capability:

```text
measure.waveform
```

Основные операции:

```text
probe
configure
single
capture
```

Plugin может вернуть:

- число точек;
- минимум;
- максимум;
- среднее;
- waveform CSV.

---

# orbita.r4831

Capability:

```text
signal.resistance
```

В коде существует serial adapter и операция:

```text
set_resistance
```

Однако текущая подтверждённая методика УБСИ рассматривает физический Р4831
как ручной магазин сопротивлений.

Поэтому:

```text
наличие автоматизирующего plugin
!=
разрешение автоматически управлять Р4831
```

До отдельного подтверждения текущий УБСИ flow должен считать действие с
Р4831 ручным операторским действием.

---

# Capability важнее модели

Сценарий должен зависеть от capability:

```text
power.dc_supply
```

а не от:

```text
AKIP1160ConcreteClass
```

Тогда прибор можно заменить другим реализационным plugin без
переписывания всей методики.

---

# Safe Stop

Активное оборудование обязано иметь определённое безопасное поведение.

Общий runtime предоставляет:

```text
safeStopAll()
```

Он вызывает `safe_stop` каждого уникального связанного устройства.

Ошибка отдельного `safe_stop` не должна мешать попытке остановить
остальные устройства.
