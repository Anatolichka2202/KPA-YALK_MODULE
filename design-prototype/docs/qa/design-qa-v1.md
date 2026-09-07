# Design QA — Station Shell / КТМА v1

Дата начала: 07.09.2026  
Branch: `design/station-shell-v1`  
Статус: **implementation active / visual QA pending**.

`design-qa.md` относится к предыдущему прототипу `App.jsx/styles.css` и не доказывает качество нового v1 entry point.

## 1. Что уже проверено на уровне репозитория

- изменения проектируются только внутри `design-prototype/`;
- старый `App.jsx/styles.css` сохранён;
- entry point переведён на `AppV1.jsx`;
- канонические UX-specs обновлены;
- F12 больше не описан как переключатель Production/Acceptance;
- demo values явно маркируются в новом UI;
- Manual Action имеет safety override: command bar остаётся доступной, обычные session-команды блокируются, Safe Stop остаётся интерактивным;
- Acceptance flow в route orchestration соединён с отдельным ReportViewer;
- Production flow соединён с отдельной production ledger, а не с acceptance report;
- Production HMI содержит переключаемые prototype states: normal / NOT_NORMAL / stale data / stand error;
- NOT_NORMAL канала сохраняется при переходе HMI → production ledger;
- stale/error блокируют обычное продолжение, но не Safe Stop;
- интерактивные prototype states созданы для `НЕ НОРМА`, stand error, stale data и busy resource;
- создан single-product startup state;
- создан permission-denied state, где F12 не является повышением прав;
- создан UX-state восстановления незавершённого сеанса после restart;
- создан multi-product launcher с partial/loading/product-error состояниями;
- Administration имеет edit-state и destructive composition confirmation;
- GitHub не запускал автоматический CI workflow для PR head, поэтому build/test status нельзя считать подтверждённым.

Все перечисленное выше — проверка структуры файлов и route/design contracts. Она **не заменяет** browser interaction QA.

## 2. Обязательный visual QA

### 1920×1080 — primary

Проверить:

- Station Home без лишнего whitespace;
- три карточки задач КТМА читаются как primary choices;
- specialized single-product startup;
- multi-product loading/error variants;
- Production queue;
- 96-channel HMI: normal / NOT_NORMAL / stale / stand error;
- selected channel, axis, actual value labels, secondary trends;
- Production ledger normal и fault variants;
- Acceptance steps 1–8;
- Manual Action;
- Acceptance ReportViewer;
- Administration workspace/edit/confirmation;
- recovery lab: NOT_NORMAL / ERROR / STALE / BUSY;
- permission denied;
- session reopen/recovery;
- F12 drawer на каждом основном route;
- command bar и Safe Stop;
- safety override во время Manual Action.

### 1664×935 — regression

Проверить соответствие исторической плотности industrial reference и отсутствие обрезки.

### 1024×768 — compact desktop

Проверить перестройку колонок и сохранение primary/safety actions.

### 720×900 — fallback

Проверить вертикальную перестройку shell. Канальный HMI может иметь локальную горизонтальную прокрутку; системная command bar должна оставаться доступной.

## 3. Interaction QA

Ниже пункты остаются незакрытыми до фактического запуска прототипа в browser/runtime.

- [ ] Station → КТМА → Production → Session → Production ledger.
- [ ] HMI NOT_NORMAL → fault ledger сохраняет `НЕ НОРМА`.
- [ ] stale/error HMI блокирует primary action, Safe Stop остаётся доступным.
- [ ] Station → КТМА → Acceptance → Session → ReportViewer.
- [ ] Station → КТМА → Administration → edit/confirmation.
- [ ] Active Session row открывает соответствующий сеанс.
- [ ] UX SCENARIOS dock открывает single-product / recovery / edge / report / admin-edit состояния.
- [ ] F12 открывает engineering drawer, route не меняется.
- [ ] повторный F12 закрывает drawer.
- [ ] Escape закрывает engineering drawer.
- [ ] Safe Stop переводит session state в STOPPED.
- [ ] после STOPPED primary action недоступен.
- [ ] Pause/Continue не меняет route.
- [ ] Manual Action confirm disabled до checkbox.
- [ ] Manual Action confirm переводит маршрут к следующему шагу.
- [ ] во время Manual Action Safe Stop доступен, остальные commandbar actions недоступны.
- [ ] destructive composition confirm disabled без причины/checkbox.
- [ ] permission request prototype не меняет route/task автоматически.
- [ ] session recovery prototype не предлагает молча начать новый RUN.
- [ ] keyboard focus видим на всех интерактивных элементах.

## 4. Accessibility QA

- [x] status = text + icon + color в design-system contract/code.
- [x] `:focus-visible` описан в CSS.
- [x] Manual Action имеет `role=dialog` и `aria-modal=true`.
- [x] destructive Administration dialog имеет `role=dialog` и `aria-modal=true`.
- [x] channel bars имеют `title` с номером/отклонением.
- [ ] focus trap для Manual Action.
- [ ] focus trap для destructive Administration dialog.
- [ ] focus return после закрытия modal flows.
- [ ] проверить контраст реальным инструментом.
- [ ] проверить screen-reader labels для icon-only элементов (если появятся).

## 5. Known open design issues

### P0 / safety

Нет известных открытых P0 на уровне design spec/code review. Safety mitigation для Manual Action реализован, но остаётся **неверифицированным визуально/интерактивно**, поэтому P0 нельзя считать закрытым для handoff до browser QA.

### P1

1. PermissionState требует подтверждения реальной модели ролей/эскалации; текущая кнопка запроса — только UX placeholder.
2. Restart/reopen screen задаёт UX contract, но реальный persistence/resource restore mechanism не подтверждён и не должен выводиться из прототипа.
3. Нужен multi-product empty state (нет доступных продуктов) отдельно от loading/error.
4. Нужен formal focus-management pattern для modal/dialog flows.

### P2

5. Фактические labels над всеми 96 bars показываются полностью только на широком desktop; проверить читаемость на реальном 1920×1080.
6. Нужна content-polish pass для сокращения длинных explanatory paragraphs в operator screens после проверки comprehension.
7. UX SCENARIOS dock должен быть исключён из будущего Qt/operator shell; это только средство навигации React-прототипа.

## 6. Build / test status

**Не подтверждён.** В draft PR нет автоматического workflow run на момент последней проверки.

До отметки `passed` необходимо фактически выполнить:

1. `npm run build`;
2. существующие Sites tests;
3. browser interaction QA;
4. visual screenshot QA на целевых viewport;
5. keyboard/accessibility pass.

Не переносить статус `passed` из legacy QA на v1 автоматически.