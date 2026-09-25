# 03: External process в общем run

**Статус:** done

**Блокируется:** 01: Project package и workflow identity; 02: Component profile и raw-sample boundary.

**Закрывает критерии:** К4.

## Что построить

Поставка может подключить существующее стендовое ПО как внешний процесс,
передать ему рабочий каталог, аргументы и окружение, получить stdout/stderr,
отменить запуск и дождаться terminate/kill fallback. Этот путь принадлежит
общему execution runtime и не создаёт отдельный runtime для конкретной
поставки.

## Критерии приёмки

- [x] Внешний процесс запускается через общий component contract.
- [x] Args, environment, cwd, stdout и stderr доступны как результат запуска.
- [x] Timeout и отмена приводят к ожидаемому завершению процесса.
- [x] Contract и scenario seams проверены автоматическими тестами.
- [ ] Реальный production consumer внешнего процесса будет подтверждён
  отдельной delivery-задачей, если появится такая поставка.

## Доказательство закрытия

Локально проходят `stand.execution_runtime` и `stand.execution_scenario`.
Незакрытая интеграция stdout/stderr с общим Evidence принадлежит задаче 06.
