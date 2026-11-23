# MilTech Station Enterprise — Software Delivery / Slice 3 (2026-11-23)

Пилотная система доставки полных подписанных поставок на автономные станции с возможностью работы через Enterprise и без сети. Включены все исходники предыдущих Slice 1/2. Ничего не изменено в настоящем MilTech C++/Qt-репозитории.

**База документации:** [`docs/delivery/ARCHITECTURE.md`](docs/delivery/ARCHITECTURE.md) — потоки поставок, online/offline, rollback, Windows, ТУ EXE. [`docs/delivery/CORE_INTEGRATION.md`](docs/delivery/CORE_INTEGRATION.md) — задачи C++/Qt Core и запуск/готовность; [`docs/delivery/SECURITY.md`](docs/delivery/SECURITY.md) — угрозы и не закрытые риски.

## Что работает

- CLI `miltech-delivery keygen/build/verify`: локальные Ed25519 ключи для *тестовой* подписи, manifest с SHA-256 для каждого файла, подтверждение формата `.mtbundle`.
- Server: `POST /api/v1/admin/delivery/releases`, versioned targets, immutable target history, auth download и idempotent reports, PostgreSQL migration `003_signed_delivery_postgres.sql`.
- Edge: HTTPS pull по station bearer, локальная перепроверка publisher signature, онлайн cache и автономный USB import.
- Installer: staging каталога версии без записи в `db/`/`config/`, запрос activation в режиме обслуживания, выбор pending boot/known-good active и подтверждение/откат.
- Status delivery Outbox: SQLite `delivery/reports.sqlite3`, потеря сети не стирает ожидающие status reports.
- Unit/integration тесты: `python -m pytest -q`. В тесте пересылается реально подписанный пакет объёмом больше 1 МиБ через FastAPI, скачивается авторизованной станцией и устанавливается в staging. EXE/DLL внутри теста — *фиктивные bytes*, не исполняемые MilTech бинарники.

## Сборка пилотной поставки

Нужна директория с готовыми **реальными** файлами по относительным путям из `examples/delivery/release-spec.example.json` (она в этом архиве не поставляется). Не пытайся выдать тестовые фиктивные bytes за готовую программу.

```powershell
pip install -e ".[test]"
miltech-delivery keygen --key-id miltech-ci-pilot --private C:\Safe\pilot-ci.pem --trust-file C:\Safe\trusted-publishers.json
miltech-delivery build --spec examples\delivery\release-spec.example.json --source C:\build\miltech\stage --private C:\Safe\pilot-ci.pem --output C:\build\miltech\station-0.4.0.mtbundle
miltech-delivery verify --bundle C:\build\miltech\station-0.4.0.mtbundle --trust-file C:\Safe\trusted-publishers.json
```

В производстве секретный ключ должен находиться в HSM/secure CI, а не в каталоге `C:\Safe`; команды иллюстрируют только пилотный путь. Release UUID в примере — демонстрационный, при новом выпуске следует генерировать новый UUID. `keygen` не перезаписывает старый ключ.

## Enterprise

```powershell
$env:MILTECH_DELIVERY_STORAGE='D:\MilTech\artifacts'
$env:MILTECH_DELIVERY_TRUST_FILE='C:\ProgramData\MilTech\Enterprise\trusted-publishers.json'
$env:MILTECH_DATABASE_URL='postgresql+psycopg://<service-account>:<secret>@<db-host>/enterprise'
$env:MILTECH_ADMIN_KEY='<pilot-admin-secret>'
# DBA: применить миграции 001..003 отдельно под миграционной ролью.
uvicorn miltech_server.main:app --host 127.0.0.1 --port 8000
```

Если нет хранилища и доверенных публичных ключей, HTTP routes доставки возвращают **503**, а не принимают неизвестный код. До реального развёртывания необходим TLS reverse proxy, нормальная авторизация, backup и проверка PostgreSQL схем.

Пример API для администратора (авторизация у пилота через `X-Admin-Key`):

```text
POST /api/v1/admin/delivery/releases
Content-Type: application/octet-stream
<raw signed .mtbundle bytes>

PUT /api/v1/admin/delivery/stations/<station_uuid>/target
Content-Type: application/json
{"release_id":"<release_uuid>","channel":"pilot"}
```

## Автономная станция

Пути примера: **разные** root-директории для immutable release и persistent data.

```powershell
$env:MILTECH_STATION_TOKEN='<station-bearer-secret>'
miltech-delivery pull --server https://enterprise.example --trust-file C:\MilTech\trusted-publishers.json --install-root 'C:\Program Files\MilTech\Station' --data-root 'C:\ProgramData\MilTech\Station' --capability qt-core

# Если связи с Enterprise нет, тот же bundle используется с USB:
miltech-delivery stage --bundle E:\station-0.4.0.mtbundle --trust-file C:\MilTech\trusted-publishers.json --install-root 'C:\Program Files\MilTech\Station' --data-root 'C:\ProgramData\MilTech\Station' --capability qt-core

# Только лабораторный пилот: требует НАСТОЯЩЕГО отсутствия испытаний.
miltech-delivery activate --release-id <release_uuid> --maintenance-confirmed --trust-file C:\MilTech\trusted-publishers.json --install-root 'C:\Program Files\MilTech\Station' --data-root 'C:\ProgramData\MilTech\Station' --capability qt-core
```

После этого внешний bootstrap должен вызвать `resolve-boot`, стартовать кандидата, дождаться health и выполнить `confirm-boot`. **Эта часть пока не соединена с Core!** Не запускать `activate` на реальном работающем испытании; флаг не проверяет процессы.

Восстановление статусов после отключения:

```powershell
miltech-delivery reports-sync --server https://enterprise.example --trust-file C:\MilTech\trusted-publishers.json --install-root 'C:\Program Files\MilTech\Station' --data-root 'C:\ProgramData\MilTech\Station'
```

## Приёмка пилота

- Работающее автономное испытание не должно зависеть от доступности Enterprise и release-сервера.
- Базы `runs.db`, `registrar.db`, `parameters.db`, станционный профиль и локальные артефакты не входят в signed bundle и не перезаписываются.
- Принятие/обновление release не является решением ОТК по изделию.
- Для первого production deployment нужно связать supervisor с действующим C++ Core, подтвердить проверку готовности прибора и отчётного слоя, провести Windows/Qt/PostgreSQL интеграционные тесты.
