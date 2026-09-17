# Контракт данных HMI УБСИ

Этот документ описывает данные, которые графический слой получает от промежуточной модели представления. Виджеты не разбирают сетевые пакеты, строки протокола или низкоуровневые события оборудования.

## 1. Общий контекст производственного прогона

```text
RunUiState
  product_serial
  operator_name
  production_stage
  scope
  runtime_state          running | waiting_operator | stand_error | stopped | finished
  current_procedure      power | yalk_analog | yalk_discrete | yalk_overload | ytp | yvp | finish
  elapsed_ms
  product_verdict        norma | ne_norma | null
  operator_comment
```

`product_verdict` приходит от исполнительной части. Интерфейс не вычисляет общий итог самостоятельно.

## 2. Нижняя телеметрическая область

```text
CurrentTelemetry
  total_current_a
  fresh                   bool
  samples[]
    timestamp
    current_a
```

Это общий ток УБСИ от АКИП. При `fresh=false` старое число не показывается как текущее.

## 3. Питание

```text
PowerUiState
  sequence_index
  sequence_count
  setpoint_v
  actual_v
  hold_elapsed_ms
  hold_duration_ms
  step_result
  passive_yalk_frame?     optional
```

Пассивная телеметрия ЯЛК не становится критерием питания.

## 4. ЯЛК — аналоговые каналы

```text
YalkAnalogFrame
  point_v
  actual_reference_v7
  stimulated_channel
  pinned_channel?
  channels[80]
    physical_address
    current_v
    min_v
    max_v
    verification_state    pending | norma | ne_norma
```

Один новый кадр обновляет весь массив. `stimulated_channel` не является источником данных остальных каналов.

## 5. ЯЛК — контактные сигналы

```text
YalkContactFrame
  point_v
  actual_reference_v7
  expected_logic
  stimulated_channel
  pinned_channel?
  channels[80]
    physical_address
    analog_v
    min_v
    max_v
    logic                  0 | 1
    verification_state
```

`0` является физическим значением, а не отсутствием данных.

## 6. ЯЛК — перегрузка

```text
YalkOverloadFrame
  polarity_v              +12 | -12
  stressed_channel
  impact_index
  impact_count
  hold_elapsed_ms
  hold_duration_ms
  channels[88]
    physical_address
    baseline_code
    current_code
    delta_code
    verification_state
```

Главная плоскость использует `delta_code`. Перегружаемый канал исключён из критерия, но остаётся видимым как текущий объект воздействия.

## 7. ЯТП

```text
YtpFrame
  resistance_point_ohm    0 | 120 | 240
  tested_channel
  pinned_channel?
  waiting_operator        bool
  channels[30]
    current_ohm
    min_ohm
    max_ohm
    verification_state
```

Ручное подтверждение Р4831 инициируется исполнительной частью. Второго независимого механизма подтверждения в UI нет.

## 8. ЯВП

Минимальный контракт утверждённого представления:

```text
YvpFrame
  tested_channel
  pinned_channel?
  unit
  channels[8]
    stimulus_value        # «подано»
    measured_value        # «измерено»
    verification_state
  procedure_context
    gain?
    frequency_hz?
    point_index?
    point_count?
```

Представление не знает источник транспортных данных и не декодирует их. Дополнительные ряды допускаются только после отдельного подтверждения их физического смысла.

## 9. Производственная сессия

```text
ProductionSessionState
  operator
  production_stage
  scope
  registry[]
    serial
    composition_count
    production_ready
  queue[]
    serial
    state                 waiting | current | completed | stopped
```

Один оператор, этап и объём относятся ко всей сессии. В очередь попадают только изделия с полным составом 4/4.

## 10. ТУ

```text
TuRunUiState
  operator
  product_serial
  readiness_state
  current_requirement
  requirements[]
    tu_id
    title
    state                 pending | running | norma | ne_norma
  measurement_view        ссылка на один из общих кадров выше
  overall_verdict
```

В ветке ТУ нет производственного предупреждения «почти у предела». `НЕ НОРМА` отдельного пункта не прекращает последующие допустимые проверки.

## 11. Администрирование

```text
Product
  serial
  components[4]
    type
    component_id?
    serial?

ComponentTransfer
  component_id
  source_product
  target_product
  comment
  timestamp
```

Перенос компонента должен быть одной атомарной операцией backend: старая активная установка закрывается и новая создаётся в одной транзакции.

## 12. Presentation-only состояние

Следующие сущности не передаются в ScenarioEngine и не меняют методику:

- закреплённый канал;
- масштаб и смещение графика;
- производственный визуальный warning;
- выбранный фильтр истории;
- состояние раскрытия интерфейса.
