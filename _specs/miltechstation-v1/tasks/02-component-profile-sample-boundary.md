# 02: Component profile и raw-sample boundary

**Статус:** done

**Блокируется:** ничем (можно брать сразу).

**Закрывает критерии:** нет — подготовительный срез для К3 и К4.

## Что построить

Станция получает состав компонентов из профиля, а не из ветвлений общего
кода. Delivery связывает logical resource с конкретным component instance и
его capability. Поток сырых отсчётов приходит через station-level sample
source и передаётся протокольному consumer через интеграционную границу; core
не выбирает конкретный board/SDK.

## Критерии приёмки

- [x] Profile различает component, provider, binding и capability.
- [x] Дублирующая декларация component id и неоднозначный binding отвергаются.
- [x] Назначенный resource проверяется на требуемую capability до запуска.
- [x] Raw samples поступают в consumer через общий bridge, а не через
  delivery-specific callback в core.
- [x] Контракты component profile, component lifecycle, station session и
  sample bridge проверяются автоматическими тестами.

## Доказательство закрытия

Локально проходят `stand.component_profile`, `stand.component_runtime`,
`stand.station_session` и `integration.orbita_sample_bridge`. Это не доказывает
поддержку произвольного нового оборудования без отдельного plugin/bench gate.
