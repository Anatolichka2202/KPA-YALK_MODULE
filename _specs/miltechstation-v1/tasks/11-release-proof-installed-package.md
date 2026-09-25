# 11: Release proof и установленный package

**Статус:** todo

**Блокируется:** 05: Lease, состояния ресурсов и recovery; 06: Сквозной immutable Evidence; 07: Project-driven operator launcher; 08: Production packages и повтор узла; 09: Free и минимальный Admin V1; 10: Environment context для normal / `+` / `−`.

**Закрывает критерии:** К11.

## Что построить

Ответственный за выпуск собирает документированный Release, устанавливает
runtime package на чистом layout и получает воспроизводимый smoke-test. CI
проверяет текущий master; delivery paths дополнительно проходят назначенные
contract, integration, bench и Evidence gates. Результат выпуска не зависит от
личной рабочей директории разработчика.

## Критерии приёмки

- [ ] Текущий master проходит обязательный CI без известных красных тестов.
- [ ] Release build создаётся документированным существующим toolchain и не
  требует случайного build tree на целевом ПК.
- [ ] Installed package на чистом layout находит project assets, profile и
  нужные plugins.
- [ ] Smoke-test покрывает startup, project selection, workflow readiness и
  сохранение тестового run.
- [ ] Для hardware-impacting delivery paths recorded bench/Evidence gates
  проверяются до выдачи статуса `CLOSED`.
- [ ] Выпуск содержит воспроизводимую версию/identity package и trace тестов.
