# 07: Project-driven operator launcher

**Статус:** todo

**Блокируется:** 01: Project package и workflow identity; 02: Component profile и raw-sample boundary; 04: Зелёный resource-routing contract.

**Закрывает критерии:** К5.

## Что построить

Оператор открывает project package, видит только workflows этого проекта,
выбирает разрешённый запуск и получает readiness нужных ресурсов. Main desktop
не содержит hard-coded выбор поставки, профиля или workflow; delivery остаётся
владельцем своей физической readiness-последовательности и специализированного
экрана.

## Критерии приёмки

- [ ] Project package определяет выбор profile и доступных workflows в UI.
- [ ] Launcher не использует hard-coded delivery workflow codes для списка
  операторских действий.
- [ ] Перед запуском показан readiness набора resources выбранного scenario.
- [ ] Отсутствующий/неисправный ресурс останавливает запуск как техническую
  проблему, не как verdict DUT.
- [ ] Есть integration test: package → workflow → scenario → RunContext.
- [ ] Установленный runtime проходит smoke-test без опоры на дерево сборки.
