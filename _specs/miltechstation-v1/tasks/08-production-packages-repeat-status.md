# 08: Production packages и повтор узла

**Статус:** done

**Блокируется:** 05: Lease, состояния ресурсов и recovery; 06: Сквозной immutable Evidence; 07: Project-driven operator launcher.

**Закрывает критерии:** К8.

## Что построить

Производственный оператор запускает `FULL`, `POWER`, `YALK`, `YTP` или `YVP`
для зарегистрированного DUT. После неуспеха в составе `FULL` он выбирает один
узел и запускает повтор; прежний run остаётся в истории. Карточка изделия
показывает текущий статус по последней принятой проверке каждого узла и не
выдаёт техническую ошибку стенда за `НЕ НОРМА` изделия.

## Реализация (2026-09-28)

- Package `projects/ktma/project.yaml` объявляет пять отдельных доступных
  production workflows: `FULL`, `POWER`, `YALK`, `YTP`, `YVP`; каждый ссылается
  на свой scenario YAML и требует registered DUT.
- Project launcher передаёт выбранный пакет в существующий production путь;
  новые binaries/engines не добавлены.
- После каждой попытки `ProductionLedger` хранит отдельную запись и
  `run_id`. Результаты `NORM`/`NOT_NORM` считаются принятыми; `STAND_ERROR`,
  `INCOMPLETE`, `STOPPED` остаются в истории и не заменяют измеренный verdict.
- Агрегатор выбирает новейший принятый результат каждого затронутого узла.
  Каждое пересчитывание сохраняет source `production_run_id`, `run_id`, статус,
  причину и timestamp в `ubsi_production_status_audit`.
- Экран регистратора показывает последнее принятое состояние отдельно для
  ЯЛК, ЯТП, ЯВП и питания; технические ошибки не отображаются как отказ DUT.
- Contract test моделирует `FULL → NOT_NORM → YALK STAND_ERROR → YALK NORM`;
  проверяет сохранение предыдущих попыток, непринятие технической ошибки и
  выбор принятого повтора как текущего результата.

Критерий текущего статуса относится к KTMA project/registrar composition;
общий RunStore остаётся generic и хранит immutable run/Evidence.

## Критерии приёмки

- [x] Workflow production требует зарегистрированный DUT согласно policy.
- [x] Пять пакетов определены конфигурацией, а не отдельными binaries.
- [x] Re-run одного узла создаёт новую immutable production-попытку и сохраняет предыдущее
  Evidence.
- [x] Агрегатор статуса использует последнюю принятую проверку именно этого
  узла и сохраняет audit trail выбора.
- [x] Отказ оборудования классифицируется отдельно от измеренного fail DUT.
- [x] Contract и integration tests покрывают `FULL → failure → node rerun →
  current status` без физического DUT.

## Проверка

- `stand.project_definition`, `ktma.ubsi.production` и `desktop.test_page_smoke`
  прошли в Release build.
- Полный CTest будет повторён после завершения оставшихся задач.
