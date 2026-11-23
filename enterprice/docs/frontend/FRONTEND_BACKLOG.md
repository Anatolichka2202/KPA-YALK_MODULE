# MilTech Station Enterprise — задачи фронтенду v0.1

Дата: 2026-11-23. P0 — фундамент, P1 — первый полезный web release, P2 — сквозной производственный контур. Статусы задачи по умолчанию OPEN; зависимости BLOCKED означают недостающий API, а не выполненную реализацию.

## A. Web — задачи фронтенда

| ID | Приоритет | Задача | Зависимость | Приёмка |
|---|---|---|---|---|
| FE-001 | P0 | Каркас React/TS strict, маршруты и модули overview, production, quality, stations, releases, administration | Нет | CI build, routing, error boundary, нет backend coupling |
| FE-002 | P0 | Industrial Precision design system, header, навигация, таблицы, формы, статус, ошибки и пустые состояния | docs/ui/UI_SPEC_V0.md | 1366/1920, keyboard/focus, нет статуса только цветом |
| FE-003 | P0 | OpenAPI client и моки с DEMO-маркировкой | contracts/openapi.v1.json | contract tests; 401/403/409/413/422/503 и offline различимы |
| FE-004 | P0 | Безопасная сессия, permission guards, запрещение admin-key в браузере | BE-001 | 403 не обходится прямым запросом; секретов в client build нет |
| FE-010 | P1 | Парк стендов: поиск, фильтры, heartbeat, версии, синхронизация | BE-002 | server-side pagination, нет выдуманного online threshold |
| FE-011 | P1 | Детали стенда: identity, profile, inventory, часы, последние runs, доставка | BE-002, BE-003 | observed_at/received_at различимы; нет прямого управления КПА |
| FE-012 | P1 | Read-only preflight и diff сетевого/аппаратного профиля | BE-003 | видны расхождения NIC/IP/VLAN, local port/owner, COM/USB/VISA |
| FE-013 | P1 | Реестр релизов и upload подписанного файла | BE-001, BE-004 | проверяем 413/422/503, нет подписи/ключа в браузере |
| FE-014 | P1 | Назначение релиза и история rollout | BE-001, BE-004 | assigned, STAGED, PENDING_REBOOT, ACTIVE, FAILED, ROLLED_BACK не смешиваются |
| FE-015 | P1 | Read-only список входящих RUN_COMPLETED | BE-005 | нет выдачи imported_runs за протокол/ОТК, фильтры и пагинация |
| FE-020 | P2 | Реестр заказов, маршрут и попытки операций | BE-006 | новая обязательная попытка не перезаписывает старую |
| FE-021 | P2 | Карточка серийного изделия и таймлайн состава | BE-006 | старая попытка показывает исходный состав, включая снятые компоненты |
| FE-022 | P2 | Дерево испытания, измерения и артефакты | BE-007 | на экране версия ТУ, шаг, значение/допуск, run_id, source и quality |
| FE-023 | P2 | Качество и подтверждение решения | BE-008 | APPROVED только после серверной policy, не из одного NORM |
| FE-024 | P2 | Производственный обзор/внимание | BE-006, BE-008 | KPI только по данным сервера, переходы к источникам |

## B. Локальный Qt frontend — в уже существующем контуре docs/ui и apps/ui_preview

| ID | Приоритет | Задача | Зависимость | Приёмка |
|---|---|---|---|---|
| FE-QT-001 | P0 | Read-only Presentation adapter к реальным StationSession/RunStore/параметрам | CORE-001 | quality/timestamp/source, GUI thread safety, нет ложных нулей |
| FE-QT-002 | P1 | Подключить HmiDocument к catalog и валидированным real bindings | FE-QT-001 | поставки изолированы, simulated/stale очевидны |
| FE-QT-003 | P1 | Виджет sync и доставки в локальной станции | EDGE-001 | связь Enterprise не блокирует испытание; версия/ошибка/очередь ясны |
| FE-QT-004 | P2 | Ограниченный ТУ-клиент и операторские три экрана | CONFIG-001 | нет engineering/admin UI; запреты выполняет Core, не только UI |

Перечисленные задачи Qt не санкционируют переписывание существующего HMI — сначала использовать реализацию и тесты apps/ui_preview.

## C. Backend/Core блокеры для frontend

| ID | Недостающий контракт | Минимальная договорённость |
|---|---|---|
| BE-001 | SSO/OIDC+BFF, RBAC, аудит и web session | Нет X-Admin-Key в SPA; read/write по permissions |
| BE-002 | Список площадок и стендов, деталь стенда, фильтры, pagination | GET /api/v1/admin/sites, /api/v1/admin/stations, /api/v1/admin/stations/{id} (предложено, НЕ существует) |
| BE-003 | Чтение desired/observed profile, inventory, preflight и диагностического diff через web auth | Нельзя использовать station bearer в браузере |
| BE-004 | Список/деталь релизов, manifest, актуальные назначения и история rollout | Сейчас существует upload, set target и station reports, но нет реестрового read API |
| BE-005 | Пагинированный поиск evidence.imported_runs, detail и статус доказательств | Сейчас лишь 100 последних runs по известной станции |
| BE-006 | Проекты, заказы, экземпляры, сборки, маршрут, операции: read/write сервисы | Таблицы PostgreSQL не равны готовому бизнес-API |
| BE-007 | Нормализованные evidence и авторизованные артефакты | RUN_COMPLETED пока содержит только metadata и manifest |
| BE-008 | Verification policy, quality decisions, acceptance, immutable history | Нет утверждённого общего правила приёмки |
| CORE-001 | Qt presentation DTO и проверяемый command dispatcher | Текущие HMI preview не подключены к hardware |
| EDGE-001 | Восстановление RunStore → station.db → outbox, maintenance guard/supervisor | Сейчас нет интеграции Core/Updater |
| CONFIG-001 | Выпуск отдельного ТУ EXE из утверждённой методики и manifest | Delivery упаковывает готовый EXE, но не собирает его |

Backend маршруты в этой таблице — предложения, не документация существующих API. Реальный DTO согласовать и добавить в OpenAPI до подключения страницы.

## D. Порядок выполнения

Этап F0: FE-001, FE-002, FE-003 — каркас и тестируемые UI-функции с честной DEMO-маркировкой; без write вызовов.
Этап F1: BE-001…BE-005, затем FE-004, FE-010…015 — первый живой web для парка стендов, принятого run и rollout.
Этап F2: BE-006…BE-008, затем FE-020…024 — заказ → изделие → состав → испытание → решение ОТК.
Этап F3: CORE-001, EDGE-001, CONFIG-001, затем FE-QT-001…004 — реальный HMI и ограниченная ТУ-сборка.

## E. Общий Definition of Done

- API и DTO версионированы; ошибки, пустое/просроченное состояние и полномочия проверены.
- Состояния симуляции/демо и действующих данных нельзя спутать.
- Нет действий, обходящих Core и maintenance guard или инициирующих оборудование из web.
- Нет браузерных секретов и несанкционированных прямых подключений к БД/OT.
- Есть автоматические тесты состояния/доступа, подтверждена работа на экранах 1366×768 и 1920×1080.
- Отдельно отмечено, что тестировали: моки, реальный API, реальный Core/стенд. Наличие mock UI не считается выполнением integration task.
