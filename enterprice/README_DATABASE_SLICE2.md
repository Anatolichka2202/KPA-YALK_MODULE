# MilTech Station — Data Layer / Slice 2 (23.11.2026)

Этот пакет — продолжение Backend Slice 1; полный предыдущий backend сохранён в той же папке и дополнен новыми файлами.

**Главная документация:** [docs/DATABASES.md](docs/DATABASES.md).

**Новая PostgreSQL миграция:** [migrations/002_enterprise_manufacturing.sql](migrations/002_enterprise_manufacturing.sql). Пилотная [001](migrations/001_edge_pilot_postgres.sql) сохранена, старый `reference/postgres_schema_v0_1.sql` — только концептуальный черновик, не запускать!

**Новая автономная БД:** [miltech_edge/station_store.py](miltech_edge/station_store.py) + миграции [001](miltech_edge/db/001_station.sql) и [002](miltech_edge/db/002_local_changes.sql).

**Новые команды:** `miltech-enterprise-migrate`, `miltech-station-db`, `miltech-legacy-probe`, `miltech-station-snapshot`.

Проверка: `python -m pytest -q` в корне backend; PostgreSQL / живой Qt Core не подключены. Все ссылки на production paths приведены для понимания, не изменяют окружение.
