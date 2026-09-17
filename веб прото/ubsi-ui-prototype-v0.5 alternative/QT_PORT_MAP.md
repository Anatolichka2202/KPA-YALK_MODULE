# Карта переноса HTML-прототипа в Qt 6 Widgets / C++

Цель переноса — воспроизвести принятый прототип, а не заново спроектировать интерфейс.

## 1. Оболочки

| HTML | Qt |
|---|---|
| `home` | `HomePage` |
| `session-v11` | новый/переработанный `ProductionSessionPage` |
| `prep` | состояние `Preparation` общего `TestPage` |
| `runtime` | постоянный `TestPage` / `ProductionWorkspace` |
| `tu-runtime` | `TuWorkspace` с общими измерительными виджетами |
| `admin` | переработанный `RegistrarPage` master/detail |

## 2. Постоянный Production workspace

Рекомендуемая структура:

```text
ProductionWorkspace : QWidget
  RunHeader
  ProcedureSidebar
  StageViewport
  BottomTelemetry
```

`StageViewport` меняет только центральное представление:

```text
PowerView
YalkAnalogView
YalkContactView
YalkOverloadView
YtpView
YvpView
FinishView
```

## 3. Измерительные виджеты

Для плотных многоканальных плоскостей предпочтителен собственный `QWidget::paintEvent()` либо один выбранный графический компонент, а не десятки вложенных `QLabel`/`QFrame`.

### `YalkAnalogView`

- 80 каналов;
- текущее значение;
- min/max whisker;
- зона допуска;
- В7 reference line;
- active/pinned;
- zoom/pan/reset.

### `YalkContactView`

- общая ось 80 каналов;
- около 70 % высоты analog;
- около 30 % digital 0/1;
- synchronized active/pinned.

### `YalkOverloadView`

- 88 адресов;
- нулевая линия;
- границы ±2;
- `delta_code` marker/stem;
- stressed channel lane;
- hover/pin;
- без второй большой плоскости.

### `YtpView`

- 30 каналов;
- min/max;
- точки 0/120/240;
- modal bridge к единственному backend manual-input contract.

### `YvpView`

- одна плоскость 8 каналов;
- пары `подано / измерено`;
- текущий и закреплённый канал;
- transport details в виджет не передаются.

## 4. ViewModel

Не связывать `QWidget` напрямую с произвольными `RunEvent.data` строками. Между исполнительной частью и HMI нужен адаптер:

```text
ScenarioEngine / ProductionLedger / Registrar
                ↓
             UiAdapter
                ↓
      типизированные UI-state структуры
                ↓
              Widgets
```

Названия и состав структур определены в `UI_DATA_CONTRACT.md`.

## 5. Цвета и геометрия

Сначала воспроизводится `prototype.html` на 1920×1080. Затем проверяется 1600×900. Нельзя «улучшать» отдельные экраны во время переноса.

## 6. Проверка соответствия

Для каждого экрана Qt делается контрольный снимок в тех же состояниях, что в `screenshots/`, и проходит тот же список из `QA_REPORT.md`.

Если Qt-виджет требует нового элемента, которого нет в прототипе, спецификации или data contract, реализация останавливается и вопрос выносится отдельно.
