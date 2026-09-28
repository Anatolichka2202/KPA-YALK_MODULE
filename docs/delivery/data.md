# Данные поставки КТМА

Продуктовая модель:

[../product/data.md](../product/data.md)

В текущей поставке данные разделены между несколькими владельцами.

---

# Карта

```text
parameters.db
    │
    ├── legacy metadata Орбиты
    └── нормализованный каталог стенда

runs.db
    │
    └── факты конкретных запусков ScenarioEngine

registrar.db
    │
    ├── изделия
    ├── состав / замены
    ├── этапы
    ├── связь этапа с run_id
    └── task-specific production ledger УБСИ
```

Эти базы нельзя механически объединять только потому, что все они SQLite.

---

# 1. parameters.db

Текущая desktop-реализация открывает рядом с приложением:

```text
parameters.db
```

В базе исторически существуют таблицы:

```text
parameters
addresses
```

`MetadataService` использует их для старого адресного справочника Орбиты.

Тот же SQLite-файл содержит нормализованный каталог стенда с таблицами
вида:

```text
catalog_meta
catalog_cell_types
catalog_block_types
catalog_block_slots
catalog_parameter_groups
catalog_bindings
catalog_streams
catalog_conversions
catalog_parameter_bindings
...
```

Конкретная схема определяется текущим `stand/src/catalog.cpp`.

## Владелец смысла каталога

Исполняемым исходником нормализованного каталога является конфигурация из
`data/catalog/`.

SQLite — рабочее представление каталога для приложения.

Не редактировать адреса вручную в Markdown.

---

# 2. runs.db

`RunStore` хранит факты выполнения ScenarioEngine.

Основные таблицы:

```text
test_runs
run_steps
run_measurements
run_events
run_evidence
run_artifacts
```

## test_runs

Хранит один верхнеуровневый запуск:

```text
run_id
scenario_id
scenario_version
catalog_version
profile_version
object_serial
started_ms
finished_ms
verdict
```

Это позволяет после испытания установить, какой именно набор сценария,
каталога и профиля участвовал в результате.

## run_steps

Хранит выполненное дерево шагов:

```text
run_id
node_id
parent_node
sort_order
title
tu_requirement
verdict
message
```

## run_measurements

Хранит измерения шага:

```text
parameter_key
title
reference
measured
lower_limit
upper_limit
unit
verdict
message
attributes
evidence_state
evidence_resource
evidence_device
evidence_quality
```

`attributes` используется процедурами для специфических данных без
изменения общей схемы на каждый новый тип измерения.

`evidence_state` имеет `NOT_PROVIDED` или `PROVIDED`. При `PROVIDED` поля
resource, device и quality обязательны. Значение `NOT_PROVIDED` является
явной фиксацией того, что процедура не передала происхождение измерения; оно
не означает, что источник был определён неявно.

## run_events

Хранит хронологию событий выполнения:

```text
timestamp
node
stage
message
verdict
```

## run_artifacts

Хранит metadata файлов, прикреплённых к run без превращения каждого raw sample
в `run_events`:

```text
sort_order
kind
relative_path
media_type
byte_count
sha256
```

`relative_path` разрешается относительно `test_runs.artifact_directory`.
`RunArtifacts::attachTo()` прикладывает собственные telemetry/raw packet
файлы, а `attachFileTo()` — любой уже созданный файл внутри того же каталога
(например stdout, stderr или waveform).
`sha256` фиксирует содержимое на момент `RunArtifacts::attachTo()`; он не
утверждает, что файл нельзя изменить позднее, а позволяет это обнаружить.

## run_evidence

Хранит упорядоченные технические события audited equipment boundary:

```text
sequence
timestamp_ms
monotonic_ns
type
node_id
resource
capability
operation
message
verdict
data
```

---

# 3. registrar.db

`registrar.db` принадлежит жизненному циклу КТМА.

Основные общие таблицы регистратора:

```text
products
components
product_components
stage_attempts
stage_runs
```

Они не дублируют измерения из `runs.db`.

Связь:

```text
stage_attempts
      │
      ↓
stage_runs.run_id
      │
      ↓
runs.db / test_runs.run_id
```

---

# 4. Task-specific данные УБСИ

Текущий production-flow УБСИ использует в том же SQLite-файле
`registrar.db` отдельные таблицы:

```text
ubsi_production_runs
ubsi_production_run_components
```

Владельцем этих таблиц является application layer КТМА/УБСИ, а не общий
RunStore и не legacy metadata Орбиты.

Подробно:

[../task/ubsi/production-data.md](../task/ubsi/production-data.md)

---

# 5. run_id — главный мост

Не копировать полную таблицу измерений в registrar.

Правильная связь:

```text
изделие
  ↓
этап / production run
  ↓
run_id
  ↓
runs.db
  ↓
шаги / измерения / события
```

`run_id` также присутствует в generated reports, чтобы пользователь или
инженер мог найти исходную запись.

---

# 6. Что не является базой истины

Не считать источником данных:

- HTML отчёт;
- CSV экспорт;
- screenshot;
- старый UI;
- Markdown-таблицу с вручную переписанными значениями.

Они могут быть представлением или документацией, но фактический run должен
сохраняться структурированно.

---

# 7. Миграции

Текущий код создаёт необходимые таблицы при открытии базы и содержит
идемпотентные доработки старых схем.

Не документировать полный SQL DDL вручную как второй независимый источник
схемы.

Канонический контракт здесь — смысл таблиц и границы владения.

Конкретный DDL остаётся в реализации.
