# 09: Free и минимальный Admin V1

**Статус:** todo

**Блокируется:** 05: Lease, состояния ресурсов и recovery; 06: Сквозной immutable Evidence; 07: Project-driven operator launcher.

**Закрывает критерии:** К9.

## Что построить

Инженер создаёт или выбирает допустимую свободную проверку, изменяет только
разрешённую project configuration и видит connections, logical resources,
оборудование и диагностику. Все действия остаются в том же resource/safety
boundary, что и production; админка не становится вторым механизмом прямого
управления транспортом.

## Критерии приёмки

- [ ] Free workflow может использовать dynamic scenario только при явном
  разрешении package.
- [ ] Совместимый resource binding выбирается без перекомпиляции и проходит
  ту же preflight validation, что формальный run.
- [ ] Admin V1 показывает project, equipment, connections, resources и
  diagnostic state без скрытого редактирования transport details из operator UI.
- [ ] Engineering override и допустимый custom tolerance сохраняются в
  context/Evidence свободного run.
- [ ] Published configuration нельзя изменить in-place; новая версия получает
  отдельную identity.
- [ ] Есть operator/engineering smoke tests на эти экраны и действия.
