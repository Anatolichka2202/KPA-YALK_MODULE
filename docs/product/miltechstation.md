# MilTechStation

## Назначение

MilTechStation — расширяемая программная платформа для создания, настройки и эксплуатации автоматизированных стендов проверки и испытаний оборудования и изделий.

Платформа должна позволять инженеру собрать новый проект из уже поддерживаемого оборудования, сценариев, экранов и отчётов без разработки отдельного приложения. Программист требуется там, где появляется новый аппаратный протокол, новый драйвер/адаптер, vendor SDK или внешний connector.

---

# Главный объект продукта

Главный объект MilTechStation — **Project**.

Проект композиционно определяет:

```text
Project
├── DUT / типы проверяемых объектов
├── equipment profile
├── connections / resource bindings
├── workflows
├── scenarios
├── project screens
├── reports
└── scripts
```

Проект не создаёт собственный Scenario Engine или собственную систему плагинов. Он настраивает общие механизмы MilTechStation.

Один физический стенд может использовать разные project packages. Одно изделие может быть объектом большого проекта или отдельного самостоятельного проекта проверки.

Технический формат project package описан в [Project package](../reference/components/project-package.md).

---

# Базовая модель выполнения

```text
Project
   ↓
Workflow
   ↓
Scenario
   ↓
Scenario Engine
   ↓
logical resource
   ↓
capability
   ↓
device plugin
   ↓
physical equipment
   ↓
measurement / event
   ↓
Evidence
   ↓
result / report
```

Физический прибор не является capability, а capability не является resource.

Пример:

```text
measure.reference        logical resource
        ↓
V7_MAIN                  concrete equipment instance
        ↓
measure.reference_voltage
measure.reference_ac_voltage
measure.reference_frequency
```

Другой проект может привязать `measure.reference` к другому прибору без изменения сценарной логики.

---

# Workflow

Workflow описывает контекст запуска поверх сценария.

Он может задавать:

- фиксированный scenario или разрешение динамического сценария;
- registration policy;
- environment descriptor;
- report template;
- ссылку на reference workflow;
- допустимость engineering overrides.

Core не содержит специальных типов `KTMA_TU` или `UBSI_PRODUCTION`. `kind` workflow является project-owned строкой.

Типовые project-level workflow могут называться `free`, `tu`, `production`, но Scenario Engine от этих названий не зависит.

## Свободный workflow

Свободная проверка использует тот же runtime, что и формальная проверка, но не обязана участвовать в lifecycle конкретного изделия.

В ней допустимы:

- динамический сценарий;
- собственный экран;
- динамические допуски;
- live-проверка без отчёта;
- собственный report template.

Отдельный `FreeModeEngine` не создаётся.

## Формальный workflow

Формальный workflow может выполняться без записи изделия в lifecycle database, но при наличии известного объекта run может быть к нему привязан.

## Производственный workflow

Производственный workflow может требовать зарегистрированный DUT и иметь отличную от формальной последовательность. При этом проект может явно задавать reference workflow, относительно которого строится финальная проверка.

---

# Сценарии

Scenario Engine выполняет последовательность предметных процедур.

Сценарий должен оперировать:

```text
resource + capability + operation
```

а не COM-портом, HTTP URL или классом конкретной модели прибора.

Для расширяемого редактора внутренняя модель сценария должна поддерживать как минимум последовательные действия, измерения, сравнения, ожидания, ветвления, циклы, параллельные ветви, operator steps, scripts, sub-scenarios и cleanup. YAML и визуальный редактор должны быть разными представлениями одной Scenario IR, а не отдельными движками.

---

# Оборудование

Физическое оборудование подключается через device plugins.

Plugin инкапсулирует протокол и опасные аппаратные операции. Project/Stand Profile определяет конкретные connections и resource bindings.

Станция должна оперировать возможностями оборудования, например:

```text
задать напряжение
измерить напряжение
измерить ток
сгенерировать сигнал
выполнить коммутацию
управлять климатической камерой
```

а не размазывать команды конкретной модели прибора по сценарию.

---

# Transport, plugin, connector, procedure, script

Термины разделяются следующим образом.

**Transport** — низкоуровневый механизм обмена: HTTP, TCP, UDP, Serial, VISA, CAN, USB, vendor SDK.

**Device plugin** — адаптер конкретного класса оборудования к station capabilities.

**Connector** — связь MilTechStation с внешней системой: MES, СУС/Fleet, customer API, внешняя БД.

**Procedure** — предметная операция сценария. Procedure не является механизмом расширения платформы.

**Script** — проектная логика на поддерживаемом script runtime.

---

# Safety boundary

Пользовательский scenario, HMI, Lua/Python script или connector не должен получать production-доступ к физическому transport напрямую.

Канонический путь:

```text
Scenario / Script / HMI
          ↓
       Resource API
          ↓
      Capability
          ↓
     Device plugin
          ↓
 safety / ownership / limits
          ↓
       Transport
```

Безопасность не должна зависеть от того, написал ли автор сценария правильный cleanup.

Platform runtime должен развиваться вокруг:

- resource ownership / lease;
- interlocks;
- equipment limits;
- safe-stop;
- явного состояния `INDETERMINATE`, если после timeout невозможно доказать физическое состояние оборудования.

---

# Environment

Внешние условия являются частью контекста испытания, а не отдельной копией каждого сценария.

Одна предметная процедура может использоваться в normal/climate/post-climate workflows.

Environment layer в дальнейшем может включать:

- ручное подтверждение условий;
- автоматическую климатическую камеру;
- непрерывную запись температуры/влажности;
- вибрационное воздействие;
- параллельный мониторинг DUT во время воздействия.

Конкретные климатические setpoints принадлежат поставке/методике, а не продуктовому документу.

---

# HMI

MilTechStation имеет несколько представлений одной системы.

**Operator HMI** ориентирован на безопасное и наглядное выполнение проверки.

**Engineering UI** должен показывать scenario tree, equipment/resource state, команды, измерения, timeline, diagnostics и причины технического отказа.

**Project screens** позволяют проекту собрать специализированное отображение из стандартных виджетов без нового Qt-приложения. Для сложных случаев допускается native UI extension/plugin.

Dashboard предназначен преимущественно для наблюдения за состоянием станции и run, а не для хранения предметной логики проверки.

---

# Результат и Evidence

Станция должна различать как минимум:

```text
изделие измерено и не выполнило требование
```

и

```text
достоверное испытание не удалось провести из-за стенда
```

Ошибка оборудования или транспорта не должна автоматически становиться браком изделия.

Первичным результатом является структурированный Run/Evidence, а не HTML/PDF.

Evidence должен постепенно стать единым источником для:

- команд и подтверждений;
- измерений;
- физических воздействий;
- параметров среды;
- действий оператора;
- safety events;
- verdict.

Документ является представлением уже сохранённых данных.

Подробнее: [Данные MilTechStation](data.md).

---

# Скрипты и расширения

Lua/Python могут использоваться в проектных и в дальнейшем production workflow, но через контролируемый Station Script API. Production script должен иметь фиксируемую identity/version/hash/runtime.

Нестабильные vendor DLL, legacy integrations и script runtimes допускается запускать out-of-process, чтобы ошибка расширения не завершала основной HMI/runtime.

---

# Fleet и HIL

Fleet/СУС и hard-real-time HIL не входят в базовый V1 runtime.

При этом архитектура не должна блокировать:

- Station API, identity, heartbeat и health для будущей СУС;
- отдельный real-time executor, запускаемый Scenario Engine, но не реализованный самим GUI/обычным scenario scheduler.

Локальный safety loop остаётся на станции.

---

# Архитектурная граница

В общем продуктовом коде не должно появляться предметных понятий конкретного проекта или изделия.

Например, в `station/` недопустимо вводить ветвление вида:

```text
if project == KTMA
if dut == UBSI
if channel_type == YALK
```

Такие знания принадлежат project/delivery/task слоям.

---

## Что НЕ относится к продуктовому уровню

В этот документ не должны попадать:

- конкретные IP-адреса поставки;
- номера каналов конкретного изделия;
- пункты конкретного ТУ;
- команды конкретного адаптера;
- конкретная технологическая последовательность одного изделия;
- операторские инструкции конкретной проверки.

---

## Связанные документы

- [Границы проекта](../00_BOUNDARIES.md)
- [Project package](../reference/components/project-package.md)
- [Данные MilTechStation](data.md)
- [Поставка КТМА](../delivery/ktma.md)
- [Текущий объект УБСИ](../task/ubsi.md)
