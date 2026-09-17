# Граница backend / model / frontend

Статус: архитектурное ограничение ветки `new-dis`.

## Цель

`master` остаётся источником истины для стендового backend: ScenarioEngine, процедуры, оборудование, протоколы, безопасные состояния и сценарии. Ветка нового интерфейса не должна менять backend ради формы представления данных.

UI/UX и presentation model являются заменяемыми потребителями backend-контракта. Вместо Qt Widgets к тому же backend может быть подключён CLI, другой GUI или автоматический runner без изменения `station/`.

## Направление зависимостей

```text
station backend / ScenarioEngine / equipment
                 |
                 | RunEvent + ScenarioRunResult + explicit equipment contracts
                 v
       application adapter / orchestration
                 |
                 v
       replaceable presentation model
                 |
                 v
          Qt Widgets frontend
```

Разрешено:

- `apps/desktop` зависит от публичных контрактов `station`;
- presentation model преобразует backend events/results в состояния отображения;
- orchestration выбирает сценарий, запускает backend и передаёт события потребителю;
- CLI может читать те же backend events/results без Qt UI model.

Запрещено:

- `station/` включает файлы из `apps/desktop`;
- backend знает о QWidget, TestPage, цветах, геометрии или состоянии экранов;
- процедура меняет алгоритм измерения только потому, что конкретному экрану нужен другой формат;
- UI-модель становится обязательной частью ScenarioEngine или equipment runtime.

## Высокочастотное исключение

Для потоков, где обычное копирование/агрегация `RunEvent -> presentation model -> QWidget` теряет временное разрешение или создаёт недопустимую задержку, допускается отдельный high-rate read path.

Он должен быть изолированным adapter/source, например:

```text
equipment/backend stream -> bounded snapshot/ring buffer -> consumer
```

Ограничения для такого пути:

1. backend всё равно не зависит от UI;
2. источник публикует данные через нейтральный контракт, без QWidget/Qt Widgets типов;
3. UI не управляет алгоритмом испытания через этот поток;
4. нормативный результат испытания формирует backend, а не график/виджет;
5. буфер ограничен по памяти и не блокирует measurement thread.

Типичный кандидат — непрерывная телеметрия всех каналов ЯЛК для визуализации. Обычные этапы, verdict, readiness, YTP/YVP progress и итоговые результаты идут через стандартный событийный контракт.

## Ветка

Финальное состояние проекта: `master` и одна параллельная ветка `new-dis`. Временная `integration/master-yvp-ui` используется только до прохождения сборки/тестов и переноса её HEAD в `new-dis`.
