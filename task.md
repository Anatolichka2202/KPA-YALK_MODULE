# Трекер MilTechStation V1

**Спецификация:** [`_specs/miltechstation-v1/spec.md`](_specs/miltechstation-v1/spec.md)

**Правило статусов:** единственный источник истины — отдельные файлы в
`_specs/miltechstation-v1/tasks/`. Эта страница — сводка и не должна менять
статус задачи самостоятельно.

| № | Задача | Статус | Блокируется |
| --- | --- | --- | --- |
| 01 | Project package и workflow identity | done | — |
| 02 | Component profile и raw-sample boundary | done | — |
| 03 | External process в общем run | done | 01, 02 |
| 04 | Зелёный resource-routing contract | done | 02 |
| 05 | Lease, состояния ресурсов и recovery | done | 04 |
| 06 | Сквозной immutable Evidence | done | 01, 03, 04 |
| 07 | Project-driven operator launcher | done | — |
| 08 | Production packages и повтор узла | done | 05, 06, 07 |
| 09 | Free и минимальный Admin V1 | done | 05, 06, 07 |
| 10 | Environment context для normal / `+` / `−` | done | 01, 06, 07 |
| 11 | Release proof и установленный package | in-progress | 05, 06, 07, 08, 09, 10 |

## Текущее состояние

Задачи 01–10 завершены как отдельные проверенные срезы; задача 11 остаётся
открытой только по физическим bench/Evidence gates для hardware-impacting
поставок. Локальные Release tests и smoke не заменяют эти gates.

Задача 05 закрыта программным контрактом: run фиксирует lifecycle состояния,
неподтверждённая остановка блокирует следующий запуск, а recovery переводит
ресурс в READY только по явному подтверждению. Старые equipment plugins с
`void safe_stop` останутся INDETERMINATE до реализации своего подтверждающего
контракта и bench/Evidence проверки; это release gate, не скрытое SAFE.
Задача 06 закрыта локальным contract: run/Evidence/measurements и metadata
raw-artifacts сохраняются, читаются обратно и формируют отчёт. Это не
подменяет bench/Evidence gates аппаратно значимых поставок.
Задача 08 закрыта конфигурационными production workflows и KTMA registrar
путём повторного узла: предыдущие попытки сохраняются, агрегатор выбирает
последний принятый результат и пишет audit trail, а `STAND_ERROR` не заменяет
измеренное состояние DUT. Это не вводит production-модель в общий station core.
Локальный Release build и полный CTest от 2026-09-28 проходят: **24/24**.
GitHub Actions run `36411212720` прошёл для базового `master` SHA
`5d4e0b74a7d23262ef3f51f1fddd07900a9caefc`; локальные изменения после этого
SHA в тот CI run не входят. Установленный package smoke прошёл из распакованного
чистого каталога вне дерева сборки и сохранил/прочитал тестовый run.
Аппаратные bench/Evidence gates этим не заменяются.

## Покрытие критериев спецификации

| Критерий | Задачи |
| --- | --- |
| К1 | 01 |
| К2 | 01 |
| К3 | 04 |
| К4 | 03 |
| К5 | 07 |
| К6 | 05 |
| К7 | 06 |
| К8 | 08 |
| К9 | 09 |
| К10 | 10 |
| К11 | 11 |
