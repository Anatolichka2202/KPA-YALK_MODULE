# УБСИ UI Prototype v0.5 — сверка с реальными сценариями master

Проверено по `master` репозитория `Anatolichka2202/KPA-YALK_MODULE` на HEAD,
который на момент сверки указывал на `f72769096b70bf3156a484a655883e583befc0db`.

## 1. Производственный full scenario

Источник: `data/scenarios/ubsi_production_full.yaml`, version 1.0.4.

Порядок:

1. `ubsi.readiness`
2. `ubsi.supply_range`
3. `yalk.start_stream`
4. `yalk.read_calibration`
5. `yalk.check_initial_state`
6. `yalk.check_channels` analog
7. `yalk.check_channels` contacts
8. `yalk.check_overload`
9. `ubsi.reference_voltage`
10. `yalk.safe_cleanup`
11. `ytp.start_stream`
12. `ytp.read_calibration`
13. `ytp.check_channels`
14. `ytp.safe_cleanup`
15. ЯВП
16. safe power off

## 2. «Обрыв» и контактные сигналы — НЕ одно и то же

Контактные сигналы:

```text
0,0 В -> 0
0,9 В -> 0
2,5 В -> 1
```

Это отдельная `yalk_contact_thresholds / yalk_contacts` проверка.

Обрыв:

```text
снять внешнее воздействие
получить свежее состояние
критерий U < 0 В
```

В current TU scenario он оформлен отдельным узлом:

```text
yalk_initial
title: Обрыв и исходное отключённое состояние 80 входов ЯЛК
tu: 1.1.4.10
procedure: yalk.check_initial_state
```

Следовательно, UI/отчёт не должны называть «обрыв» контактной проверкой.

## 3. Power и пассивный ЯЛК

`ubsi.supply_range` сейчас:

- управляет АКИП;
- публикует setpoint / actual voltage / total current;
- на 24/27/35 проверяет `ulk.parameter_source.alive`;
- после survival воздействий возвращает 27 В и восстанавливает ЯЛК.

Он **не публикует full 80-channel snapshot** во время supply.

При этом readiness перед supply уже запускает ЯЛК/ROKT. Поэтому passive full-array observation на 24/27/35 технически естественно добавить в backend, но сейчас это **ещё data-contract gap**.

Методика отдельно фиксирует, что при 19 В адаптер может потерять обмен. Поэтому live ЯЛК на 19 В нельзя делать критерием. UI может показывать best-effort/freshness.

Для 37 В current method также оценивает работоспособность после возврата к 27 В; live массив во время самой выдержки не является нормативным обязательством.

## 4. Power timing

Scenario:

```text
24 / 27 / 35 В: settle_ms = 500
19 В: 300 s
37 В: 60 s
restore: 27 В
recovery_timeout_ms = 30000
```

Prototype v0.5:

```text
19 В / 300 s -> 20 s ТОЛЬКО в prototype
37 В / 60 s -> 60 s
остальное без глобального time scale
```

## 5. ЯЛК analog/discrete

Scenario points:

```text
analog:   0 / 3.1 / 6.2 В
contacts: 0 / 0.9 / 2.5 В
sample_count = 16
settle_ms = 150
channel_off_settle_ms = 1000
```

ВАЖНО: текущий backend `yalkCheckChannels` всё ещё реализует channel-major:

```text
for channel:
    for point:
        воздействие
```

Утверждённый UI/target scenario требует point-major:

```text
for point:
    for channel:
        воздействие
```

Это реальный backend rework, а не косметика прототипа.

## 6. Overload — проверено по реальному C++

`yalk.check_overload` в current master делает:

1. `makeSafe()`:
   - source off;
   - `full_reset`;
   - DAC off всех mapped channels;
   - `cleanup_settle_ms = 300`.
2. `applyReferenceStaircase()`.
3. wait `baseline_settle_ms = 1000`.
4. один `baseline = readFreshYalkSnapshot(...)`.
5. ещё раз `makeSafe()`.
6. внешний цикл polarity:
   - `+12 В`;
   - затем `-12 В`.
7. внутренний цикл target `1..88`.
8. для target:
   - восстановить reference staircase;
   - включить overload route;
   - отключить analog DAC текущего target;
   - подключить target;
   - ждать `overload_settle_ms = 10000`;
   - снять fresh snapshot;
   - сравнить остальные 87 каналов с исходным baseline;
   - target из сравнения исключён;
   - `makeSafe()`.

Текущий критерий:

```text
abs(delta_code) <= 2
```

Таким образом пользовательская формулировка верна:

```text
baseline
RESET / DAC OFF
+12 В: 1..88
-12 В: 1..88
```

Prototype v0.5 визуализирует baseline как постоянную «тень» и current/last snapshot как основной столбец.

## 7. ЯТП

Current backend:

```text
for resistance point:
    backend modal operator.manual_input
    wait 1500 ms
    for channel 1..30:
        read 16 samples
```

То есть 1,5 с — stabilization **один раз после установки Р4831**, а не на каждый канал.

Prototype v0.5 это учитывает.

## 8. ЯВП — текущая реальность master

В master появился новый production backend `V7 + ИСД`:

```text
Rigol -> 1000 pF -> вход ЯВП
выход ЯВП -> ЯЛК/ИСД -> В7
```

Standalone scenario `ubsi_production_yvp.yaml` version 1.3.0:

```text
8 каналов
gains = 0.25,0.5,1,2,4,8,32
frequencies = 0.15,20,250,500,1800,2000,4000
settle = 200 ms
active_outputs_confirmed = true
mapping_confirmed = false
```

Backend matrix:

```text
8 channels × 7 gains × 7 frequencies = 392 points
```

Минимум только explicit settle:

```text
392 × 0.2 s = 78.4 s
```

плюс команды/измерения.

Но **current standalone scenario intentionally stops INCOMPLETE before active switching**
because `mapping_confirmed=false`.

## 9. Несостыковка full/TU vs standalone ЯВП

Standalone YVP уже переведён на новый V7+ISD backend.

`ubsi_production_full.yaml` и `ubsi_ulk_combined_check.yaml` на момент сверки
ещё содержат старые ROKT/YALK-oriented YVP args/titles.

При этом `ubsi.yvp` alias в current backend уже указывает на V7+ISD implementation.

Это надо синхронизировать в scenarios до реального full/TU run.
UI не должен решать эту несостыковку самостоятельно.

## 10. Prototype policy

v0.5 показывает:

- реальную структуру power/YALK/YTP/overload;
- target point-major ЯЛК, потому что это уже утверждённое требование;
- target YVP matrix с явной пометкой backend gap;
- passive YALK on power как target data-contract extension, а не как уже существующий event.

Никакие неподтверждённые значения не должны переноситься в Qt как реальные backend fields.


## Production stages (v0.5)

Current `registrar::Stage` contains seven new-production values:

```text
Primary
ClimateNormal
ClimateMinus
ClimatePlus
PottingClimateNormal
PottingClimatePlus
PottingClimateMinus
```

Legacy electrical stage values are not offered for new Production Sessions. The selected stage is session-wide and is carried into Preparation, runtime context, Finish, production report and history. Human-readable delivery labels are deliberately not inferred from enum names.
