# 09: Free и минимальный Admin V1

**Статус:** done

**Блокируется:** 05: Lease, состояния ресурсов и recovery; 06: Сквозной immutable Evidence; 07: Project-driven operator launcher.

**Закрывает критерии:** К9.

## Что построить

Инженер создаёт или выбирает допустимую свободную проверку, изменяет только
разрешённую project configuration и видит connections, logical resources,
оборудование и диагностику. Все действия остаются в том же resource/safety
boundary, что и production; админка не становится вторым механизмом прямого
управления транспортом.

## Критерии приёмки

- [x] Free workflow может использовать dynamic scenario только при явном
  разрешении package.
- [x] Совместимый resource binding выбирается без перекомпиляции и проходит
  ту же preflight validation, что формальный run.
- [x] Admin V1 показывает project, equipment, connections, resources и
  diagnostic state без скрытого редактирования transport details из operator UI.
- [x] Engineering override и допустимый custom tolerance сохраняются в
  context/Evidence свободного run.
- [x] Published configuration нельзя изменить in-place; новая версия получает
  отдельную identity.
- [x] Есть operator/engineering smoke tests на эти экраны и действия.

## Выполнено и проверено (2026-09-28)

- Home screen добавляет общий read-only Admin V1: профиль/проект, connections,
  компоненты и bindings, lifecycle resources и diagnostics; прямых команд
  оборудованию из окна нет.
- Free запускается только через workflow, разрешающий dynamic/override;
  выбранный YAML проходит ту же ScenarioEngine validation и ресурсную
  preflight-проверку до запуска.
- Run хранит описание/инженерную заметку, hash и неизменяемый снимок YAML как
  artifact. Tolerance values остаются частью конфигурации сценария, не
  подменяются текстом UI.
- Опубликованный YAML нельзя сохранить поверх себя; новый черновик получает
  отдельный путь/identity. UI smoke проверяет блокировку save для published.
- Release build и `desktop.test_page_smoke`, `stand.project_definition`,
  `stand.run_store_context` прошли.
