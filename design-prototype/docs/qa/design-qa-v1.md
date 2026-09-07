# Design QA — Station Shell / КТМА v1

Дата начала: 07.09.2026  
Branch: `design/station-shell-v1`  
Статус: **implementation started / visual QA pending**.

`design-qa.md` относится к предыдущему прототипу `App.jsx/styles.css` и не доказывает качество нового v1 entry point.

## 1. Что уже проверено на уровне репозитория

- изменения изолированы внутри `design-prototype/`;
- старый `App.jsx/styles.css` сохранён;
- entry point переведён на `AppV1.jsx`;
- канонические UX-specs обновлены;
- F12 больше не описан в specs как переключатель Production/Acceptance;
- demo values явно маркируются в новом UI;
- GitHub не запустил автоматический CI workflow для head commit после создания draft PR, поэтому build/test status нельзя считать подтверждённым.

## 2. Обязательный visual QA

### 1920×1080 — primary

Проверить:

- Station Home без лишнего whitespace;
- три карточки задач КТМА читаются как primary choices;
- Production queue;
- 96-channel HMI: значения, selected channel, axis, secondary trends;
- Acceptance steps 1–8;
- Manual Action;
- Administration;
- F12 drawer на каждом основном route;
- command bar и Safe Stop.

### 1664×935 — regression

Проверить соответствие исторической плотности industrial reference и отсутствие обрезки.

### 1024×768 — compact desktop

Проверить перестройку колонок и сохранение primary/safety actions.

### 720×900 — fallback

Проверить вертикальную перестройку shell. Канальный HMI может иметь локальную горизонтальную прокрутку; системная command bar должна оставаться доступной.

## 3. Interaction QA

- [ ] Station → КТМА → Production → Session.
- [ ] Station → КТМА → Acceptance → Session.
- [ ] Station → КТМА → Administration.
- [ ] Active Session row открывает соответствующий сеанс.
- [ ] F12 открывает engineering drawer, route не меняется.
- [ ] повторный F12 закрывает drawer.
- [ ] Escape закрывает engineering drawer.
- [ ] Safe Stop переводит session state в STOPPED.
- [ ] после STOPPED primary action недоступен.
- [ ] Pause/Continue не меняет route.
- [ ] Manual Action confirm disabled до checkbox.
- [ ] Manual Action confirm переводит маршрут к следующему шагу.
- [ ] keyboard focus видим на всех интерактивных элементах.

## 4. Accessibility QA

- [x] status = text + icon + color.
- [x] `:focus-visible` описан в CSS.
- [x] Manual Action имеет `role=dialog` и `aria-modal=true`.
- [x] channel bars имеют `title` с номером/отклонением.
- [ ] focus trap для Manual Action.
- [ ] focus return после закрытия Manual Action.
- [ ] проверить контраст реальным инструментом.
- [ ] проверить screen-reader labels для icon-only элементов (если появятся).

## 5. Known open design issues

### P0 / safety

1. **Safe Stop во время Manual Action**: полноэкранный dialog перекрывает нижнюю command bar. До design sign-off нужно либо добавить Safe Stop внутрь Manual Action, либо спроектировать safety layer выше modal. Обычные session-команды при этом не должны становиться кликабельными поверх dialog.

### P1

2. Нужен отдельный UI-пример `NOT_NORMAL` на channel overview.
3. Нужен stand-error recovery flow с различием `НЕ НОРМА` и `ОШИБКА`.
4. Нужен equipment-conflict resolution flow: кто владеет ресурсом и что оператор может сделать.
5. Нужен специализированный single-product startup state.
6. Administration пока является архитектурным prototype; edit forms и destructive confirmations нужно проектировать отдельным slice.

### P2

7. Фактические labels над всеми 96 bars показываются полностью только на ≥1800 px; на меньшей ширине показываются разреженно + selected. Проверить читаемость на реальном 1920×1080.
8. Нужна content-polish pass для сокращения длинных explanatory paragraphs в operator screens после проверки comprehension.

## 6. Build / test status

Не подтверждён. В draft PR нет автоматического workflow run на момент создания файла.

До отметки `passed` необходимо фактически выполнить build/tests и визуальный просмотр. Не переносить статус `passed` из legacy QA на v1 автоматически.
