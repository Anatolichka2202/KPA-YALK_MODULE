# 04: Зелёный resource-routing contract

**Статус:** done

**Блокируется:** 02: Component profile и raw-sample boundary.

**Закрывает критерии:** К3.

## Что построить

Автор workflow выбирает конкретный logical resource, runtime проверяет его
capability и выполняет процедуру через выбранный экземпляр. Несовместимая,
отсутствующая или неоднозначная привязка завершается до физического
воздействия понятной технической ошибкой. Этот сквозной путь должен быть
стабильно зелёным в чистом локальном окружении и CI.

## Критерии приёмки

- [x] `stand.scenario_resource` стабильно проходит из обычного CTest запуска.
- [x] Тест создаёт читаемый временный scenario asset или использует иной
  воспроизводимый fixture; причина нынешней ошибки установлена доказательно.
- [x] Named resource направляет invocation только в выбранный component.
- [x] Missing, incompatible и ambiguous binding не допускают процедуру до
  опасного вызова plugin.
- [x] Общий CTest после изменения не получает новый регресс в profile/runtime
  seams.

## Доказательство закрытия

Причина отказа была в чтении UTF-8 пути временного YAML через узкий
`std::ifstream` на Windows/MinGW под кириллическим профилем. Loader теперь
использует Qt file API с UTF-8 декодированием, а regression fixture всегда
создаёт каталог с кириллицей. После Release-сборки проходят
`stand.scenario_resource` и весь набор generic station тестов; полный CTest
даёт 22/24. Два оставшихся отказа относятся к KTMA/UBSI, а не к routing
contract: `ktma.ubsi.equipment_readiness` (`0xc0000135`) и
`ktma.ubsi.scenario_runtime` (YALK open-state criterion).
