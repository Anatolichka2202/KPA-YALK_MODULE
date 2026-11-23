# Проверка Software Delivery Slice 3

**2026-11-23.** Команда: `python -m pytest -q` из `backend/`.

## Проверено автоматически

- 45 тестов всего (существующие тесты Backend Slice 1 / Database Slice 2 + новый delivery suite).
- Сборка и Ed25519-подпись многокомпонентной поставки с `.exe`, `.dll`, JSON сценария и каталога (фиктивные тестовые bytes, не MilTech runtime).
- Проверка доверенного издателя и SHA-256 каждого файла, запрет unsigned/foreign-signed/tampered packages, ZIP path traversal и дополнительных файлов.
- Установка в отдельный immutable release directory без записи в локальную `runs.db`.
- Платформенная и capability проверка при staging.
- Первый запуск release, двухфазное подтверждение, консервативный fallback при потере boot confirmation и ручное назначение rollback.
- Enterprise API: импорт одного signed bundle размером более 1 МиБ (прежний лимит 1 МиБ не блокирует бинарный upload), version assignment, revision history, station bearer download, disabled-station denial.
- Дедупликация доставленных отчётов по event_id; локальный SQLite журнал статусов сохраняет несданные сообщения при потере сети.
- Недоверенное или не сконфигурированное серверное delivery отключено с 503.

## Не проверено

- Реальная сборка C++/Qt MilTech Core и настоящие ТУ EXE; их нет в доступном исходном репозитории этого пакета.
- Windows ACL, exe locking, сервис, bootstrap, реальные предохранители активных испытаний и IFW binarycreator.
- Запуск PostgreSQL 16+ и миграции `003_signed_delivery_postgres.sql` на реальной БД (проверена лишь статическая доступность миграции в manifest).
- Формальная криптографическая/безопасностная экспертиза, HSM, Authenticode, mTLS/SSO/RBAC.
- Нагрузочные тесты больших пакетов, параллельная работа сотен стендов, частичное скачивание Range/ETag, отказ диска и аппаратная симуляция потери питания.

## Production gate

Это **инженерный pilot**, не готовое решение для развертывания на работающем производственном стенде. Без интегрированного Core maintenance guard в ручном CLI нет достоверного сигнала об отсутствии активного испытания; `--maintenance-confirmed` — только демонстрационный предохранитель. Не использовать на рабочем КПА до интеграции supervisor и отдельной приёмки в Windows/OT.
