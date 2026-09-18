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

Для потоков, где обычное копирование/агрегация `RunEvent -> presentation model -> QWidget` **измеренно** теряет необходимое временное разрешение или создаёт недопустимую задержку, допускается отдельный high-rate read path.

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

Текущий `master` уже публикует во время поканального чтения диагностический `BACKGROUND`-кадр: backend агрегирует `mean/min/max` одновременно по всем словам принятого кадра и после штатной калибровки отдаёт его как `RunEvent`. Поэтому `new-dis` сначала использует этот штатный контракт для общей плоскости ЯЛК и ЯТП. Параллельный reader того же UDP-потока только ради UI не добавляется.

Отдельный high-rate source вводится только если на реальном стенде будет измерено, что частоты `BACKGROUND` недостаточно для требуемого отображения. В этом случае он остаётся read-only относительно методики и не заменяет backend verdict/event path.

## Ветка

Финальное состояние проекта: `master` и одна параллельная ветка `new-dis`. Временная `integration/master-yvp-ui` используется только до прохождения сборки/тестов и переноса её HEAD в `new-dis`.
