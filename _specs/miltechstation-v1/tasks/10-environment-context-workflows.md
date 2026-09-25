# 10: Environment context для normal / `+` / `−`

**Статус:** todo

**Блокируется:** 01: Project package и workflow identity; 06: Сквозной immutable Evidence; 07: Project-driven operator launcher.

**Закрывает критерии:** К10.

## Что построить

Оператор выбирает отдельный workflow normal, climate `+` или climate `−`.
Каждый run хранит явный descriptor среды, источник подтверждения и действия
оператора. Общая платформа не угадывает нормативные setpoint'ы: проект либо
подтверждает условия вручную, либо предоставляет совместимый chamber plugin.

## Критерии приёмки

- [ ] Environment descriptor является частью project/workflow/run context.
- [ ] Normal, `+` и `−` запускаются как отдельные runs, не как post-climate
  flag предыдущего run.
- [ ] Ручное подтверждение среды записывается как operator action/Evidence.
- [ ] При наличии chamber plugin его capability проходит общий resource/safety
  boundary.
- [ ] Неизвестный setpoint не появляется автоматически из product core.
- [ ] Есть simulator/integration tests; реальный chamber path получает
  отдельный bench verification до `done`.
