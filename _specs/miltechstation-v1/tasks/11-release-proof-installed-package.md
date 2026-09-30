# 11: Release proof и установленный package

**Статус:** in-progress

**Блокируется:** 05: Lease, состояния ресурсов и recovery; 06: Сквозной immutable Evidence; 07: Project-driven operator launcher; 08: Production packages и повтор узла; 09: Free и минимальный Admin V1; 10: Environment context для normal / `+` / `−`.

**Закрывает критерии:** К11.

## Что построить

Ответственный за выпуск собирает документированный Release, устанавливает
runtime package на чистом layout и получает воспроизводимый smoke-test. CI
проверяет текущий master; delivery paths дополнительно проходят назначенные
contract, integration, bench и Evidence gates. Результат выпуска не зависит от
личной рабочей директории разработчика.

## Критерии приёмки

- [x] Текущий master проходит обязательный CI без известных красных тестов.
- [x] Release build создаётся документированным существующим toolchain и не
  требует случайного build tree на целевом ПК.
- [x] Installed package на чистом layout находит project assets, profile и
  нужные plugins.
- [x] Smoke-test покрывает startup, project selection, workflow readiness и
  сохранение тестового run.
- [ ] Для hardware-impacting delivery paths recorded bench/Evidence gates
  проверяются до выдачи статуса `CLOSED`.
- [x] Выпуск содержит воспроизводимую версию/identity package и trace тестов.

## Выполнено локально; hardware gate остаётся открытым (2026-09-28)

- `master`/`HEAD` на момент проверки: `5d4e0b74a7d23262ef3f51f1fddd07900a9caefc`;
  GitHub Actions run `36411212720` завершился `success` для этого SHA.
- Документированная Qt 6.8 / MinGW Release сборка завершилась успешно;
  полный CTest: 24/24.
- Создан локальный, явно не привязанный к коммиту пакет
  `MilTechStation-KTMA-2.0.0-local-uncommitted-task-close.zip` в `build/deploy`;
  SHA-256 `7412d5d0ff760fda3ec6b5cce34111f724a524524db4c43efcb56a78c2c81028`.
- Распакованный smoke CLI проверяет project/profile load, Free workflow, запуск fake
  capability, запись и чтение RunStore в package `runs/`, находясь в отдельном
  каталоге установки, вне build tree.
- Task остаётся `in-progress`: hardware-impacting delivery Evidence gates
  требуют отдельной подтверждённой стендовой проверки. CI относится к базовому
  SHA `master`; локальные незакоммиченные изменения в этот CI run не входят.
