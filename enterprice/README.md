> **Slice 3 (доставка версий, 23.11.2026):** см. [README_DELIVERY_SLICE3.md](README_DELIVERY_SLICE3.md).
> Подписанные полные релизы, Enterprise назначение, офлайн-установка, activation/rollback и очередь статусов; исходные Slice 1/2 сохранены.

# MilTech Station Enterprise — Backend / Autonomous Station Pilot

**Рабочий исследовательский срез (Slice 1, 23.11.2026).** Не production-ready, не внедрён в живой Core. Цель: реализовать серверный приём результатов автономных стендов, договор окружения, базовую топологию и проверяемый контур доставки. Миграция существующих `runs.db`, `registrar.db` не выполнена.

## Что работает

- FastAPI `/api/v1`: отдельная регистрация площадки/станции и однократная выдача станционного токена, ротация/блокировка; валидируемые и версионируемые profiles; heartbeat с inventory.
- SQLite development DB; SQL PostgreSQL pilot migration (`migrations/001_edge_pilot_postgres.sql`) с совместимыми по именам сущностями первого проекта Enterprise; не выполнялась на живом PostgreSQL.
- Идемпотентный endpoint `/api/v1/sync/events`: принимаем только `RUN_COMPLETED`, строгая station sequence, конфликт UUID/содержимого, `run` сохраняется в immutable arrival staging `evidence.imported_runs`. Это не утверждение годности изделия.
- Local SQLite Outbox: commit+WAL, повторная доставка без удаления неподтверждённых событий, корректная обработка потери сети, отказ от не-HTTPS для удалённого сервера.
- `miltech-edge preflight` сравнивает объявленные NIC/IP/CIDR/ports с наблюдаемым окружением. Никаких изменений ОС.
- ResourceBroker proof of concept: эксклюзивное владение bound socket и файловая блокировка hardware resource; C++ интерфейс для будущего Core в `include/miltech`.
- Инженерная документация в `docs/TOPOLOGY.md`, `docs/STATION_CONTRACT.md`; образец YAML и Run JSON в `examples/`.

## Быстрый запуск на разработческой машине

```bash
cd miltech_enterprise_impl
python -m venv .venv
# Windows: .venv\\Scripts\\activate
# Linux: source .venv/bin/activate
python -m pip install -e '.[test]'
export MILTECH_DATABASE_URL='sqlite:///./miltech-dev.sqlite3'
export MILTECH_ADMIN_KEY='REPLACE_WITH_LONG_RANDOM_DEV_SECRET'
# PowerShell: $env:MILTECH_DATABASE_URL='sqlite:///./miltech-dev.sqlite3'
# PowerShell: $env:MILTECH_ADMIN_KEY='...'
miltech-db init-dev-db
uvicorn miltech_server.main:app --host 127.0.0.1 --port 8000
```

API documentation: `http://127.0.0.1:8000/docs` (только разработческая машина).

Проверь `GET /health/ready`, затем `POST /api/v1/admin/sites` с `X-Admin-Key`, `POST /api/v1/admin/stations` с тем же ключом, получи однократный `station_token`. Не записывай станционный токен в Git или в скрипты. Профиль загружается через `PUT /api/v1/admin/stations/{uuid}/profile` с JSON-версией `examples/station-01.example.yaml`. При необходимости PowerShell `ConvertFrom-Yaml` недоступен штатно: используй Python/PyYAML для конвертации.

## Локальная станция (reference agent)

```bash
miltech-edge preflight --profile examples/station-01.example.yaml
miltech-edge init --db ./station-data/sync/outbox.db
miltech-edge enqueue --db ./station-data/sync/outbox.db --run-json examples/run-completed.example.json
export MILTECH_STATION_TOKEN='station_token_from_admin_registration'
miltech-edge sync --db ./station-data/sync/outbox.db --url http://127.0.0.1:8000
```

В примере профиль намеренно не соответствует произвольному тестовому ПК и preflight должен возвращать `IP_NOT_ASSIGNED`: **не меняй IP ОС ради демо**. `http://127.0.0.1` разрешён только для локальной разработки. Для удалённого сервера обязателен HTTPS. `run-completed.example.json` — демонстрационный RUN, не реальный результат аппаратного испытания. В production путь outbox должен быть в каталоге данных физической установки, не в release.

## PostgreSQL deployment (архитектурная подготовка, не проверенная миграция)

- Создать выделенную on-prem PostgreSQL БД и сервисную роль, через инфраструктурную миграционную роль выполнить `migrations/001_edge_pilot_postgres.sql` один раз после аудита существующих схем.
- Запретить приложению DDL на runtime-учётных данных, настроить миграции/backup/TLS/ACL и reverse proxy (TLS) для API.
- `python -m pip install -e '.[postgres]'` добавляет драйвер psycopg 3. Пример URL только синтаксический: `postgresql+psycopg://SERVICE_USER:SECRET@db-host/enterprise` — реальные реквизиты только через secret store.
- Не применять `reference/postgres_schema_v0_1.sql` как миграцию автоматически — это сохранённый **проектный черновик** полной модели, включая production/quality сущности.

## Диагностика Windows без изменений

`powershell -ExecutionPolicy Bypass -File windows/Survey-MilTechStation.ps1` (только если это разрешено политиками предприятия). JSON-файл содержит внутренние IP и инвентарные идентификаторы; хранить с ограничением доступа. Для реальной настройки IP/VLAN нужен отдельный утверждённый installer/provisioner.

## Тестирование

```bash
python -m pytest -q
```

Тесты включают HTTP API, IAM pilot, профиль, конфликт порта, автономный Outbox и повторную передачу; выполняются на SQLite без физического оборудования. Проверка на PostgreSQL, Windows/Qt, реальных КПА, сетевом оборудовании и Windows Installer **не проводилась**.

## Недостающие production блоки

- SSO/RBAC и mTLS, резервные ключи/ротация на каждой стороне, ограничение частоты запросов, security hardening, подписанные утверждённые профили, audit write-once, секретное хранилище.
- Blob transfer/hash verification, reconciler с `ops.units` и `evidence.test_runs`, immutable object store, policy-based acceptance.
- Реальный адаптер существующей MilTech `RunStore` / `Registrar` (требуется доступ к актуальному HEAD и миграциям).
- Переход Python edge reference на полноценный Windows service/Qt C++ агент, код для TU EXE IPC, Windows networking provisioning с rollback.
- Multi-process SQLite Outbox и recovery после частично успешного Core→Outbox commit (пока Core и Outbox — две разных БД); заполнение из локальных committed runs после recovery.
- Отдельное тестирование и утверждение сетевых ACL, адресного плана, диск-квот и offline retention.

## Машинные контракты / инфраструктура

- `contracts/openapi.v1.json` — экспорт OpenAPI для генерации Qt/C++ или TypeScript клиента.
- `contracts/station-profile.v1.json` — JSON Schema конфигурации интерфейсов/портов/аппаратных ресурсов.
- `contracts/run-completed.v1.json` и `contracts/sync-event.v1.json` — JSON Schema автономной доставки.
- `examples/topology.example.yaml` — версия исходной топологии для инфраструктурного проектирования, **не факт обследования предприятия**.

---

## Продолжение: Databases / Slice 2 (23.11.2026)

В этот комплект добавлен полный следующий слой хранения для Enterprise PostgreSQL и автономной SQLite станции. См. **[README_DATABASE_SLICE2.md](README_DATABASE_SLICE2.md)** и **[docs/DATABASES.md](docs/DATABASES.md)**. Предыдущий текст этого README описывает Slice 1; для итогового статуса реализации баз данных первичен Slice 2.
