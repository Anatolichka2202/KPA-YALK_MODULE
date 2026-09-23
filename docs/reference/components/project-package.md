# Project package

`ProjectDefinition` — продуктовый контракт, который собирает один проект проверки поверх общего MilTechStation runtime.

Код контракта:

```text
station/include/orbita_stand/project.h
```

Загрузчик:

```text
loadProjectPackage(project.yaml)
```

## Граница

Project package не является новым runtime для испытаний. Он композиционно связывает уже существующие части:

```text
Project
  ├── equipment profile
  ├── DUT types
  ├── workflows
  ├── screens
  ├── reports
  └── scripts
```

Scenario Engine, Equipment Registry и StationSession остаются общими механизмами платформы.

В `station/` не должны появляться понятия конкретного проекта: `KTMA`, `UBSI`, `YALK`, `YTP`, `YVP`, `PPB`.

## `project.yaml`, schema 1

Минимальный файл:

```yaml
schema: 1
id: example
title: Example project
version: 1.0.0
equipment_profile: ../data/profiles/stand.yaml

workflows:
  - workflows/free.yaml
```

Поддерживаемые поля текущей schema 1:

- `id` — стабильный идентификатор проекта;
- `title` — отображаемое имя;
- `version` — версия project package;
- `equipment_profile` — путь к `StandProfile`;
- `dut_types` — поддерживаемые типы проверяемых объектов;
- `workflows` — список файлов workflow;
- `screens` — опциональные project screens;
- `reports` — опциональные report assets;
- `scripts` — опциональные scripts.

Все пути разрешаются относительно каталога `project.yaml` и после загрузки хранятся нормализованными.

## Workflow, schema 1

Пример формального workflow:

```yaml
schema: 1
id: tu_normal
title: Проверка по ТУ
kind: tu
scenario: ../../data/scenarios/example.yaml
environment: environments/normal.yaml

registration:
  required: false
  attach_if_found: true
```

Текущий контракт содержит:

- `id`;
- `title`;
- `kind` — непрозрачная для core строка, семантика принадлежит проекту;
- `scenario` — фиксированный сценарий;
- `environment` — опциональный environment descriptor;
- `report` — опциональный report asset;
- `reference_workflow` — ссылка на другой workflow проекта;
- `allow_dynamic_scenario` — разрешает workflow без заранее заданного scenario;
- `allow_scenario_overrides` — разрешает engineering-level изменение сценарного состава;
- `registration.required`;
- `registration.attach_if_found`.

Core не содержит enum `TU`/`PRODUCTION`: эти значения могут использоваться в `kind`, но не меняют продуктовую модель типов.

## Registration policy

Политика регистрации принадлежит workflow, а не Scenario Engine.

Типовые композиции:

```text
free
required=false
attach_if_found=false

TU
required=false
attach_if_found=true

production
required=true
attach_if_found=true
```

Это позволяет одной и той же сценарной инфраструктуре обслуживать свободные, формальные и производственные проверки без отдельного `FreeModeEngine`.

## Валидация

`loadProjectPackage()` проверяет:

- обязательные `id/title/version`;
- наличие equipment profile;
- наличие хотя бы одного workflow;
- уникальность workflow id;
- существование фиксированных scenario/environment/report assets;
- наличие scenario, если `allow_dynamic_scenario=false`;
- корректность `reference_workflow`;
- существование явно перечисленных screen/report/script assets.

## Текущая реализация КТМА

Первый package находится в:

```text
projects/ktma/project.yaml
```

Он объявляет УБСИ как текущий DUT и содержит workflow:

```text
free
tu_normal
tu_climate
production
production_climate
```

Физические подключения и resource bindings не дублируются в package: они по-прежнему принадлежат единственному `StandProfile`, на который ссылается проект.

Климатические descriptor-файлы в текущем этапе только фиксируют место environment layer. Конкретные нормативные setpoints не добавляются до подключения подтверждённой климатической методики.
