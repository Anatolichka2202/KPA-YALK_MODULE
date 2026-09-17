# UBSI UI Prototype v0.5 — production-complete prototype

Интерактивный UX/state prototype по `UBSI_UI_UX_DESIGN_SPEC_MERGED_v1.1`.


## Production stages added in v0.5

Production Session now exposes all **7 current `registrar::Stage` values**:

```text
Primary
ClimateNormal
ClimateMinus
ClimatePlus
PottingClimateNormal
PottingClimatePlus
PottingClimateMinus
```

The selected stage is fixed for the whole Production Session and is shown in Preparation, runtime header, Finish, production report and Administration history examples.

The prototype intentionally does **not** derive temperatures or human-readable names from enum codes. The UI specification requires those labels to come from delivery configuration. Until that configuration is confirmed, v0.5 displays the canonical stage code rather than inventing labels.

## Сохранённые изменения v0.4

Прототип больше не ускоряет весь run глобально.

Время идёт 1:1 с прототипом. Исключение только для длинной выдержки питания:

- реальная 19 В / 5 минут → **20 секунд в prototype**;
- 37 В / 1 минута → **60 секунд**, без ускорения.

Остальные явные выдержки показываются близко к scenario:

- ЯЛК analog/discrete: целевой point-major, ~1,15 с на канал в prototype;
- overload: baseline 1 с, RESET/DAC OFF 0,3 с, **10 с на каждый impact**, все +12 В 1..88, затем все −12 В 1..88;
- ЯТП: 1,5 с stabilization после Р4831, затем отображаемое считывание каналов;
- ЯВП target matrix: 8 × 7 коэффициентов × 7 частот, explicit settle 0,2 с на точку.

`PROTO` остаётся, чтобы не ждать получаса при визуальной проверке overload.

## Исправлено

- баг `ОШИБКА СТЕНДА → продолжить`:
  - теперь ошибка реально приостанавливает run;
  - `Повторить / продолжить` перезапускает simulation;
  - `Завершить безопасно` переводит в финальное состояние.
- Power дополнен пассивным 80-channel ЯЛК overview.
  - Это **не criterion питания**.
  - На 19 В prototype показывает потерю freshness, потому что реальная методика не гарантирует UDP во время этого воздействия.
- Overload перестроен по текущей реализации:
  - baseline один раз до перегрузки;
  - safe reset + DAC OFF;
  - +12 В по всем 88 каналам;
  - затем −12 В по всем 88;
  - 10 секунд на impact;
  - baseline остаётся полупрозрачной тенью;
  - current/last snapshot накладывается сверху;
  - stressed channel исключён из критерия.
- YVP view теперь отображает целевую матрицу 8×7×7 и явно показывает, что current master блокируется `mapping_confirmed=false`.
- Production finish корректно может показать `НЕПОЛНАЯ`, а не притворяться `НОРМА`.

## Важное

Это всё ещё UI prototype, а не замена backend.

`REAL_SCENARIO_ALIGNMENT.md` содержит проверку prototype против текущего master и перечисляет места, которые реально требуют backend rework.
