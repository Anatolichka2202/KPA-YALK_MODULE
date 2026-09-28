# Трекер MilTechStation V1

**Спецификация:** [`_spec/_spec.md`](_spec/_spec.md)

**Правило статусов:** единственный источник истины — отдельные файлы в
`_specs/miltechstation-v1/tasks/`. Эта страница — сводка и не должна менять
статус задачи самостоятельно.

| № | Задача | Статус | Блокируется |
| --- | --- | --- | --- |
| 01 | Project package и workflow identity | done | — |
| 02 | Component profile и raw-sample boundary | done | — |
| 03 | External process в общем run | done | 01, 02 |
| 04 | Зелёный resource-routing contract | done | 02 |
| 05 | Lease, состояния ресурсов и recovery | in-progress | 04 |
| 06 | Сквозной immutable Evidence | in-progress | 01, 03, 04 |
| 07 | Project-driven operator launcher | todo | 01, 02, 04 |
| 08 | Production packages и повтор узла | todo | 05, 06, 07 |
| 09 | Free и минимальный Admin V1 | todo | 05, 06, 07 |
| 10 | Environment context для normal / `+` / `−` | todo | 01, 06, 07 |
| 11 | Release proof и установленный package | todo | 05, 06, 07, 08, 09, 10 |

## Текущее состояние

Завершены четыре ограниченных программных среза: project/workflow contract,
component/sample boundary, execution runtime и resource-routing contract. Это подтверждено их
контрактными тестами, но не делает продукт готовым.

Задача 05 выполняется: declared-resource lease уже включён в common run и
Desktop-маршруты. Состояния ресурса и recovery ещё не реализованы, поэтому
задача не закрыта.
Задача 06 выполняется: сохранённый run теперь читается обратно и из него
строится отчёт; raw-artifact metadata и measurement identity ещё отсутствуют.
Локальный Release build и полный CTest от 2026-09-28 проходят: **24/24**.
Это локальный gate; статус удалённого CI и bench/Evidence подтверждения этим
прогоном не заменяются.

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
