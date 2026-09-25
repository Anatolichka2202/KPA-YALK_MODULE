# 01: Project package и workflow identity

**Статус:** done

**Блокируется:** ничем (можно брать сразу).

**Закрывает критерии:** К1, К2.

## Что построить

Инженер может загрузить project package, выбрать описанный в нём workflow и
получить один run через общий Scenario Engine. До физического запуска runtime
отклоняет package с отсутствующими обязательными полями, несуществующими
assets, неуникальными workflow или некорректными ссылками. Fixed и dynamic
workflow имеют явную policy; registration policy остаётся свойством workflow.

## Критерии приёмки

- [x] Некорректный project package не создаёт физический run.
- [x] Валидный package несёт устойчивую identity проекта и workflow.
- [x] Fixed и dynamic workflow проходят через общий Scenario Engine, без
  отдельного движка для свободного режима.
- [x] Project/workflow identity передаётся в run context.
- [x] Контракт проверен автоматическим test seam загрузки package и workflow.

## Доказательство закрытия

Локальный контракт `stand.project_definition` проходит. Реализация и
регрессия уже существуют в общем station runtime. Общий current CI не
подтверждён этим результатом, поэтому закрыт именно срез, а не весь продукт.
