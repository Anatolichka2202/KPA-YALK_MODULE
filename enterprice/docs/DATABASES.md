# MilTech Station: Enterprise PostgreSQL / Autonomous SQLite (Slice 2)

**Дата:** 2026-11-23. **Статус:** исполняемая прототипная реализация (не включена в production Core, PostgreSQL на живом сервере не прогонялся).

## 1. Источники истины и владельцы данных

| Предмет | Источник истины | Копия/ссылка |
|---|---|---|
| Модель, ревизия ТУ, маршрут, производственный заказ, состояние выпуска | Enterprise PostgreSQL (`catalog`, `ops`, `quality`) | Подписанные ограниченные по сроку задания в `station.db` |
| Исторический факт выполнения измерения | MilTech Core `runs.db` | `outbox.db` → `evidence.imported_runs` и затем `evidence.test_runs` |
| Состав изделия во время конкретного измерения | Неизменяемый snapshot конкретного run/attempt | Enterprise `evidence.test_runs.composition_snapshot` |
| Изделие и история комплектующих до подключения Enterprise | КТМА `registrar.db` в историческом режиме | Новые записи сопоставляются с `ops.units`, `ops.unit_installations`; оригинал не удалять |
| Каталог, текущие адреса и параметры | `parameters.db` в существующей Core-реализации | Версионные метаданные в артефактах run |
| Физические IP, COM, USB, VISA, UDP/TCP bindings | Конфигурация конкретной установки + ОС | `station.db` хранит утверждённое *желаемое* состояние и наблюдения; владелец сокета — ResourceBroker |
| Принятый сервером run | `evidence.imported_runs` | Не является автоматической приёмкой изделия |
| Заключение о соответствии / разрешение выпуска | Версионируемые `quality.operation_decisions`, `quality.acceptance_records` | Не выводить из `NORM` автоматически |

Не объединять все существующие SQLite файлы в одну БД. Это нарушит границы модулей и создаст сложную безостановочную миграцию.

## 2. Размещение на Windows

```text
C:\ProgramData\MilTech\Station\
    instance.json
    config\
    db\
        parameters.db             # существующий параметрический каталог
        runs.db                   # существующий RunStore
        ktma\registrar.db         # task-specific жизненный цикл КТМА
        edge\station.db          # новый автономный operational store
        edge\outbox.db           # существующий автономный sync outbox
    reports\
    logs\
    backups\
```

`C:\Program Files\...\releases\<version>` — только immutable binaries / software definitions. Ни одна production БД не заменяется seed при обновлении. Путь задаётся существующим планируемым `StationPaths`/`--data-dir` (в реальном Core адаптер ещё не подключён).

## 3. PostgreSQL: управление производственным циклом

Миграции: `migrations/001_edge_pilot_postgres.sql` (предыдущий intake-контур), затем `002_enterprise_manufacturing.sql` (новый учёт). **Не выполнять `reference/postgres_schema_v0_1.sql` поверх этих двух миграций:** это черновик альтернативной начальной схемы с пересекающимися именами таблиц.

### Основные сущности

- `infra`: площадки, стенды, привязанное измерительное оборудование и документы калибровки/поверки;
- `integration`: station profiles, credentials, sync inbox, sequence checkpoints, legacy ID mapping;
- `identity`: идентификаторы пользователей и роли; пилотное API пока не предоставляет корпоративное SSO;
- `catalog`: типовые изделия, ревизии, методики проверки/ТУ, маршруты и операции;
- `ops`: серийные экземпляры (`units`), исторические установки компонентов, производственные задания, операции, подписанные назначения стендам;
- `evidence`: входящие run, связанные с идентифицированными изделиями run, измерения, артефакты, использованные приборы;
- `quality`: несоответствия, заключения по операциям, решения о выпуске;
- `audit`: append-only события.

### Критические ограничения

- `ops.units` уникально по `(model_id, serial_number)`, ревизия принадлежит тому же типу;
- `ops.unit_installations` содержит интервалы установки. PostgreSQL `EXCLUDE USING gist` запрещает пересекающиеся интервалы одновременно для конкретного компонента и для `parent_unit_id + slot_code`;
- маршрут операции соответствует маршруту производственного заказа — через составные FK;
- нельзя привязать один `run_id` к двум независимым попыткам операции;
- `evidence.imported_runs` и нормализованные свидетельства не обновляются и не удаляются обычным DML (триггеры);
- решения о приёмке не перезаписываются: создаётся новая запись с `supersedes_id`;
- для `APPROVED` обязательны подписант в системе и хеш manifest-а доказательств. *Полномочия подписанта и сама корректность policy требуют application-level проверки*;
- учёт регистратора КТМА связывается через `integration.legacy_id_map`, без выдуманных универсальных enum для стадий всех изделий.

Остались service-level инварианты: запрет циклических сборок по произвольной глубине, проверка принадлежности стенда площадке, applicability калибровки в момент измерения, подтверждение действующей нормативной policy, авторизация пользователя.

### Управляемое применение

Только на **новую выделенную базу PostgreSQL** с привилегиями DDL у отдельной migration-role:

```powershell
pip install -e ".[postgres]"
miltech-enterprise-migrate --dry-run
miltech-enterprise-migrate --dsn "postgresql://MIGRATION_ROLE@PG_HOST/MILTECH_DATABASE"
```

Пароли указывать через защищённые средства хранения, не в истории команд. Процедура записывает SHA-256 каждой миграции в `integration.schema_migrations`, работает транзакционно, отвергает уже существующую неучтённую baseline-схему. Сопоставление имеющейся БД и backup — обязательные предварительные мероприятия. Migration-role не равна runtime-role.

**В этой среде тестового PostgreSQL и драйвера psycopg нет; SQL ещё не выполнен на реальном сервере.** Проверены только наличие схемных контрактов/порядок миграций и ранее выполненный FastAPI SQLite dev-контур.

## 4. Автономная SQLite: станция

Новая `station.db` живёт отдельно от `runs.db`, `registrar.db`, `parameters.db`. Она не является второй корпоративной master-БД изделий.

Таблицы миграции `001_station.sql`:

| Таблица | Смысл |
|---|---|
| `station_identity` | Только один уникальный зарегистрированный `station_id` |
| `config_profiles` | Неизменяемые версии конфигурации + выбор активной |
| `profile_interfaces` | NIC/role/VLAN/IP/CIDR/gateway/default route |
| `profile_sockets` | IP:port/protocol/direction/owner для манифеста ресурсов |
| `profile_resources` | COM/USB/VISA/CAN и др. аппаратные ресурсы поставки |
| `equipment_bindings` | Фактическая привязка прибора к физическому endpoint |
| `offline_assignments` | Подписанные Enterprise-задания с ограниченным сроком |
| `local_attempts` | Попытки, привязка run, snapshot состава и профиля |
| `committed_run_refs` | Неизменяемая ссылка на подтверждённый Core run |
| `local_audit` | Append-only журнал событий станции |

Миграция `002_local_changes.sql`: `local_changes` (событие + sequence + SHA-256 в транзакции с результатом попытки), `incoming_directives`, `environment_snapshots`, `broker_resource_events`. Здесь хранятся факты приобретения/освобождения ресурсов, **а не сами OS-level locks**.

- `PRAGMA journal_mode=WAL`, `PRAGMA synchronous=FULL`, `PRAGMA foreign_keys=ON`, `busy_timeout`;
- SQLite `STRICT` и `CHECK`, индексы уникальности, триггеры защиты immutable payload/завершённых attempts;
- миграции 1→2 с хеш-контролем, идемпотентные при повторном запуске;
- single-writer sqlite транзакции `BEGIN IMMEDIATE`, предотвращение конкурирующих локальных изменений;
- signed assignment проверяется Ed25519 **при сохранении и при старте** (публичный ключ — доверенная внешняя конфигурация агента, а не содержимое скачанного задания);
- профиль станции проходит `Profile` validation и read-only network preflight; применение IP/VLAN/firewall не делается;
- `Outbox` прежней версии обновляется совместимо, с `PRAGMA user_version=1`, без очистки событий.

Сроки действия подписанных назначений и версий профиля должны контролироваться исходя из синхронизированного времени; контроль доверенного времени, rollback protections от атак на ОС/диск, отзыв офлайн-заданий и ротация trust anchor остаются отдельными задачами безопасности.

## 5. Порядок фиксации результата и восстановление

```text
Core ScenarioEngine -> commit runs.db
                         |
       RunStore adapter подтверждает существование run (НЕ готов)
                         v
                  local_attempt + committed_run_ref
                  + local_changes (одна transaction station.db)
                         |
             bridge_run_to_outbox -> outbox.db
                         |
                idempotent sync via HTTPS
                         v
    integration.sync_inbox + evidence.imported_runs (Enterprise)
                         |
   validated unit mapping / normalized evidence / quality decision
```

Между `runs.db` и `station.db`, а также между `station.db` и `outbox.db` нет общей атомарной транзакции. Поэтому **для production необходимо** внедрить reconciler: периодически повторно перечислять committed Core runs и сравнивать с `committed_run_refs` и `outbox` до полной сходимости. В этом пакете есть идемпотентный метод мостика, но автоматический RunStore scanner ещё отсутствует. Не заявлять exactly-once сквозную доставку до реализации scanner и интеграционных испытаний.

После серверного ACK локальный run не удаляется. `local_changes` пока не отправляются на сервер — протокол для production events не реализован; попытка и её хронология сохраняются локально.

## 6. Создание базы станции

```powershell
pip install -e ".[test]"
miltech-station-db --db "C:\ProgramData\MilTech\Station\db\edge\station.db" enroll --station-id "<UUID>" --code "STAND-01" --site "FACTORY-01"
miltech-station-db --db "C:\ProgramData\MilTech\Station\db\edge\station.db" verify
```

Команда `profile-cache` принимает утверждённый JSON профиля, ревизию и ожидаемый хеш, которые Enterprise уже возвратил через API. Она не меняет реальный сетевой стек. Для первичной настройки требуется безопасная выдача ID/ключа станции, как в предыдущем slice.

### Read-only исследование существующих БД

```powershell
miltech-legacy-probe --data-root "C:\ProgramData\MilTech\Station"
```

Показывает обнаруженные SQLite-таблицы и поля. Никаких миграций или переписывания прежних `run_id` не делает. Проверка входных структур выполнена по доступной проектной документации, не по live HEAD.

## 7. Резервное копирование

Не делать `copy runs.db` во время открытого WAL. Доступны SQLite backup API и SHA-256 manifest:

```powershell
miltech-station-snapshot --data-root "C:\ProgramData\MilTech\Station" --destination "D:\Backups\MilTech\station_20261123" --confirm-quiesced
```

**Обязательное условие:** сначала перевести Core, registrar, агент и приложения ТУ в режим обслуживания (quiescence). `--confirm-quiesced` означает подтверждение оператора; скрипт не умеет принудительно остановить стенд. Каждый SQLite файл копируется штатным SQLite backup API. Отдельная консистентность файлов гарантируется SQLite, **глобальная межбазовая консистентность не гарантируется без остановки пишущих процессов**. Необходимы отдельные backup policy, шифрование, проверка восстановления, retention и права доступа.

## 8. Задачи интеграции с актуальным Core

1. Сверить актуальную DDL `runs.db`, `registrar.db`, `parameters.db` и существующие `run_id`/serial model; никакого импорта без map-а и аудита.
2. Реализовать C++/Qt адаптер к `StationStore`/outbox и `RunStore` либо перенести локальную станционную часть в C++ с эквивалентными миграциями.
3. Создать межбазовый reconciler и безопасный checkpoint для внезапного сбоя питания.
4. Выделить ключи подписи Enterprise в безопасный secrets service, внедрить выдачу/отзыв подписанных assignments и защищённый bootstrap агента.
5. Обеспечить воспроизводимость, иммутабельность артефактов, допуск прибора по калибровке, утверждённую policy и workflow ОТК.
6. Выполнить full integration на PostgreSQL, Windows/Qt, реальном оборудовании, сетях с изолированными адресами и отключением Enterprise.

Возможна локальная автономная работа без сервера, если имеется действительное подписанное задание и все необходимые аппаратные ресурсы. Окончательное решение о выпуске изделия остаётся в корпоративном контуре качества.
