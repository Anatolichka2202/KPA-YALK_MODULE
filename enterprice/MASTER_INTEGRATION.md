# MilTech Station — Enterprise/Autonomous/Delivery (Slice 1–3)

Дата комплекта: 2026-11-23. Этот каталог подготовлен для размещения **в `enterprice/` ветки `master` репозитория `Anatolichka2202/KPA-YALK_MODULE`**. Написание `enterprice` сохранено точно по запросу.

## Состав

- `miltech_server/` — пилот FastAPI, регистрация и профили стендов, приём RUN, Delivery API.
- `miltech_edge/` — автономный reference агент, station.db и локальный Outbox.
- `miltech_delivery/` — подпись `.mtbundle`, онлайн/USB доставка, staging и возврат к предыдущему release.
- `migrations/` — PostgreSQL SQL 001–003 (**не исполнялись на реальном PostgreSQL**).
- `templates/` — образцы SQLite схем станции и outbox, **не данные действующего стенда**.
- `contracts/`, `examples/`, `include/`, `windows/`, `installer/`, `docs/` — схемы API и сетевых профилей, Qt интерфейсы, диагностика, спецификации.
- `tests/` — тесты Python reference реализации.

## Ограничения

Это **исследовательский код**, пока не интегрированный в C++/Qt Core. Реальные бинарники, ТУ/оборудование и промышленный Windows bootstrap отсутствуют; online production deployment требует отдельной проверки безопасности и PostgreSQL. Не считать существующие интерфейсы станции реализованными только потому, что присутствуют `.hpp` контракты.

## Как проверить

```powershell
cd enterprice
py -m pip install -e '.[test]'
py -m pytest -q
```

Детали: `README.md`, `README_DATABASE_SLICE2.md`, `README_DELIVERY_SLICE3.md`. Исходники импортированы непосредственно через GitHub; локальный `Import-To-Master.ps1` в репозитории не требуется.

## Работа с каталогом репозитория

Весь код находится строго под `enterprice/`; другие файлы и каталоги `liborbita` не изменяются. При следующих изменениях проверять текущий `master` и вести линейную историю коммитов. Существующие `db` и `config` станции не заменять комплектными `templates`.
