# 10: Environment context для normal / `+` / `−`

**Статус:** done

**Блокируется:** 01: Project package и workflow identity; 06: Сквозной immutable Evidence; 07: Project-driven operator launcher.

**Закрывает критерии:** К10.

## Что построить

Оператор выбирает отдельный workflow normal, climate `+` или climate `−`.
Каждый run хранит явный descriptor среды, источник подтверждения и действия
оператора. Общая платформа не угадывает нормативные setpoint'ы: проект либо
подтверждает условия вручную, либо предоставляет совместимый chamber plugin.

## Критерии приёмки

- [x] Environment descriptor является частью project/workflow/run context.
- [x] Normal, `+` и `−` запускаются как отдельные runs, не как post-climate
  flag предыдущего run.
- [x] Ручное подтверждение среды записывается как operator action/Evidence.
- [x] При наличии chamber plugin его capability проходит общий resource/safety
  boundary.
- [x] Неизвестный setpoint не появляется автоматически из product core.
- [x] Есть simulator/integration tests. Chamber plugin path в текущем объёме не
  сконфигурирован; его будущая физическая реализация потребует отдельный bench
  verification до закрытия именно этого пути.

## Выполнено и проверено (2026-09-28)

- Project package содержит отдельные workflow ТУ normal / climate `+` /
  climate `−` и production climate `+` / `−`; у каждого свой descriptor.
- Для `manual_or_controlled` runtime не запускает сценарий без подтверждения и
  источника подтверждения. Descriptor, подтверждение и `ENVIRONMENT` Evidence
  попадают в `ScenarioRunResult`; KTMA launch добавляет их в сохраняемый run.
- Температурные setpoint/stabilization не заданы и не вычисляются общим кодом.
  Chamber plugin в текущем package не сконфигурирован, поэтому физическая
  chamber-ветка отсутствует и не заявляется как проверенная.
- `stand.project_definition` проверяет normal/+/- IDs, отказ без подтверждения
  и run context/Evidence после подтверждённого исполнения.
