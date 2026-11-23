# Проверка Slice 1 — 23.11.2026

## Что выполнено

- Автоматические unit/integration тесты: **17 passed** (pytest 9.x; Linux Python 3.13, SQLite test database).
- Сквозной smoke-test с **настоящим HTTP сервером uvicorn** на `127.0.0.1`, а не только ASGI TestClient: создание site/station → сохранение события Outbox → CLI sync → сервер ACK → чтение imported run → проверка sequence = 1. **PASS**.
- Сгенерирован OpenAPI 3.x и Pydantic JSON Schema для profile/event/run.
- Проверка профилей: OT gateway запрещён; повторная socket binding спецификация отклонена; mismatch CIDR выявляется; `bind_exclusive` держит socket handle; FileLease блокирует одновременный захват внутри cooperating processes.

## Что НЕ проверено

- Запуск SQL `001_edge_pilot_postgres.sql` на реальном PostgreSQL (сервер/драйвер в среде отсутствуют).
- Сборка C++ заголовка внутри MilTech Core; header-only proposal не компилировался с Qt.
- Windows Survey (выполнялась Linux-среда), драйверы 802.1Q, фактическое размещение IP/ACL.
- ТУ, реальные аппаратные каналы, сохранение runs в актуальном HEAD MilTech Station.
- mTLS/SSO/RBAC, подписанные профили, архив binary artifacts.

## Ограничения пилота

`evidence.imported_runs` — **arrival staging**, не утверждённый `evidence.test_runs` и тем более не запись ОТК. Профиль публикуется сервером, а прикладной CLI читает пример YAML — автоматическая доставка/проверка подписи профиля пока не реализована. Outbox и RunStore — разные БД; атомарного commit между ними нет. Для production требуется reconciler и резервное копирование.
