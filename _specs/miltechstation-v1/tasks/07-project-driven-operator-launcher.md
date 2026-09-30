# 07: Project-driven operator launcher

**Статус:** done

**Блокируется:** 01: Project package и workflow identity; 02: Component profile и raw-sample boundary; 04: Зелёный resource-routing contract.

**Закрывает критерии:** К5.

## Что построить

Оператор открывает project package, видит только workflows этого проекта,
выбирает разрешённый запуск и получает readiness нужных ресурсов. Main desktop
не содержит hard-coded выбор поставки, профиля или workflow; delivery остаётся
владельцем своей физической readiness-последовательности и специализированного
экрана.

## Критерии приёмки

- [x] Project package определяет выбор profile и доступных workflows в UI.
- [x] Launcher не использует hard-coded delivery workflow codes для списка
  операторских действий.
- [x] Перед запуском показана конфигурация и readiness набора resources выбранного scenario.
- [x] Отсутствующая привязка ресурса к capability в выбранном profile блокирует
  запуск; физическая ошибка классифицируется как техническая, не как verdict DUT.
- [x] Есть integration test: package → workflow → scenario → RunContext.
- [x] Установленный runtime проходит smoke-test без опоры на дерево сборки.

## Выполнено в текущем срезе

- Project package загружается в `KtmaMainWindow` до инициализации station
  runtime; `equipment_profile` пакета используется для создания профиля.
- Домашний экран строит список запусков из project workflows. Delivery-owned
  `operator_action` направляет выбранный workflow в специализированный экран;
  неподдержанный workflow отображается, но не запускается.
- Universal free launcher принимает workflow ID из пакета, показывает его
  scenario requirements и блокирует запуск, если capability не привязана к
  включённому equipment component выбранного profile.
- `stand.project_definition` проверяет package → workflow → scenario → run
  context; `desktop.test_page_smoke` проверяет список UI workflow, блокировку
  недоступного workflow, preview ресурсов и dispatch выбранного ID.
- `package_stand_win11.ps1` включает `projects/` и package-relative `data/`.
  Release directory запускался вне build tree: заголовок проекта загружен,
  проектный манифест и профиль присутствуют, штатное закрытие завершилось с
  кодом 0.
- Полный Release CTest: 24/24 passed.

## Осталось для закрытия

Критерии задачи закрыты. Environment workflow для климатических условий
пока отображаются отключёнными и отслеживаются в task 10.
