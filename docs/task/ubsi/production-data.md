# Production data УБСИ

Этот документ относится только к текущему объекту УБСИ.

Он не определяет общий MilTechStation и не должен автоматически
использоваться для других изделий КТМА.

---

# ProductionRunContext

Перед production run создаётся контекст:

```text
productId
productSerial
stage
package
scenarioCode
composition snapshot
```

Текущие пакеты:

```text
FullUbsi
PowerConsumption
Yalk
Ytp
Yvp
```

Пакет определяет, какая часть состава затронута конкретным run.

---

# Snapshot состава

Текущий production flow требует snapshot состава изделия до начала
измерения.

Для каждого компонента фиксируется:

```text
component_id
component_type
serial_number
affected
```

`affected` означает:

> входит ли этот компонент в текущий пакет проверки.

Snapshot нужен для исторической достоверности.

Если после run компонент заменили, старый production report всё равно
должен показывать состав, который реально стоял во время измерения.

---

# Текущая проверка состава

Текущий код требует:

```text
composition.size() == 4
```

перед созданием production run.

Это task-specific правило текущей реализации УБСИ.

Оно не является общим правилом MilTechStation.

---

# Таблицы

В `registrar.db` текущий `ProductionLedger` создаёт:

```text
ubsi_production_runs
ubsi_production_run_components
```

## ubsi_production_runs

Хранит:

```text
id
product_id
product_serial
stage
package_code
scenario_code
status
run_id
opened_at
finished_at
```

`run_id` связывает production lifecycle с `runs.db`.

## ubsi_production_run_components

Хранит неизменяемый snapshot состава:

```text
production_run_id
component_id
component_type
serial_number
affected
```

---

# Жизненный цикл production run

```text
build context
    ↓
begin()
    ↓
запись snapshot
    ↓
ScenarioEngine run
    ↓
attachRun(run_id)
    ↓
finish(status)
```

После завершения run нельзя снова закончить его другим статусом через тот
же lifecycle operation.

---

# Статусы production run

```text
IN_PROGRESS
NORM
NOT_NORM
STAND_ERROR
INCOMPLETE
STOPPED
```

Они соответствуют смыслу ScenarioEngine verdict, но являются отдельным
task-level представлением production lifecycle.

---

# Production report

Для run формируется:

```text
Производственный_отчет_<run_id>.html
Производственный_отчет_<run_id>.csv
```

Он содержит snapshot состава и измерения конкретного запуска.

Общая отчётность КТМА:

[../../delivery/reporting.md](../../delivery/reporting.md)

---

# Чего этот слой не решает

ProductionLedger не должен самостоятельно определять:

- требования ТУ;
- формулы ЯЛК;
- протокол ROKT;
- карту ЯВП;
- внешний вид operator UI;
- финальную нормативную policy всего изделия.

Это другие владельцы знания.

---

# Следующий документ

Требования и покрытие УБСИ будут собраны отдельно:

```text
docs/task/ubsi/tu.md
docs/task/ubsi/tu-work.md
docs/task/ubsi/testing.md
```
