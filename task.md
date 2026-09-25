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
| 05 | Lease, состояния ресурсов и recovery | todo | 04 |
| 06 | Сквозной immutable Evidence | todo | 01, 03, 04 |
| 07 | Project-driven operator launcher | todo | 01, 02, 04 |
| 08 | Production packages и повтор узла | todo | 05, 06, 07 |
| 09 | Free и минимальный Admin V1 | todo | 05, 06, 07 |
| 10 | Environment context для normal / `+` / `−` | todo | 01, 06, 07 |
| 11 | Release proof и установленный package | todo | 05, 06, 07, 08, 09, 10 |

## Текущее состояние

Завершены четыре ограниченных программных среза: project/workflow contract,
component/sample boundary, execution runtime и resource-routing contract. Это подтверждено их
контрактными тестами, но не делает продукт готовым.

Ближайшая незакрытая задача — 05: lease, состояния ресурсов и recovery.
Локальный полный CTest от 2026-09-25 после исправления Unicode-пути даёт
22/24. Оставшиеся отказы относятся к КТМА/УБСИ:
`ktma.ubsi.equipment_readiness` (`0xc0000135`) и
`ktma.ubsi.scenario_runtime` (YALK open-state criterion).

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
