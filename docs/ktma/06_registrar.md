# Регистратор жизненного цикла

Исходный код: `registrar/include/registrar.h` и `registrar/src/registrar.cpp`.
Цель — библиотека `ktma_registrar`, а не отдельный сервис. Она открывает
`registrar.db` отдельным Qt SQL-соединением и не меняет `parameters.db` или
`runs.db`.

## API

```cpp
createProduct(type, serial)
createComponent(type, serial)
installComponent(productId, componentId)
removeComponent(productId, componentId, reason)
beginComponentStage(productId, componentId, stage)
attachRun(stageAttemptId, runId)
finishStage(stageAttemptId, verdict)
productVerdict(productId)
```

`beginStage(productId, stage)` сохранён для агрегатных/совместимых сценариев;
при приёмке ячейки используется `beginComponentStage`.

## Таблицы

- `products` — изделие и заводской номер;
- `components` — экземпляры ячеек и их SN;
- `product_components` — история установки/снятия, active, время и причина;
- `stage_attempts` — этап, ячейка, вердикт и время;
- `stage_runs` — уникальная связь попытки с `run_id` из `runs.db`.

Конструктор создаёт таблицы и добавляет отсутствующие колонки в старой схеме;
удаление данных и автоматическое объединение баз не выполняется.

## Итог

Для каждой активной ячейки проверяются четыре обязательных этапа. Отсутствие
этапа или открытая попытка дают `Incomplete`; `Fail` имеет приоритет; все
активные ячейки с четырьмя `Ok` дают `Ok`. Снятые ячейки остаются в истории и
не блокируют новый состав после документированной замены.

Завершить этап с вердиктом `Ok` или `Fail` без привязанного `run_id` нельзя. Пустой состав
или состав, в котором все ячейки сняты, всегда даёт `Incomplete`.

В текущем UI регистратор пока только просматривает `registrar.db`. Создание изделия/ячеек,
выбор этапа и автоматическая привязка завершённого прогона ещё не подключены.

Тест `registrar_lifecycle_test` проверяет запрет двух активных ячеек одного
типа, FAIL старой SN, замену, повторный полный цикл, сохранение истории и
запрет двойной привязки запуска.
