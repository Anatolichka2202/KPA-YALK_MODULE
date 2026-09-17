# Миграция Station / Delivery / Product UI

База: `integration/master-yvp-ui`
Рабочая ветка: `architecture/platform-delivery-ui`

## Инварианты

- `master` остаётся production backend truth для УБСИ;
- утверждённый UI УБСИ в этой миграции не передизайнивается;
- зависимость только `station -> delivery -> product`;
- product UI может зависеть от delivery UI, station UI от product UI не зависит;
- администрирование принадлежит станции, форматы и runtime подключаются providers;
- executable runtime запрещён, пока его явно не разрешила поставка;
- `universal_miltechstation` и `isd_driver_safe` — доноры отдельных решений,
  а не кандидаты на wholesale merge.

## Этап 1 — граница композиции

- [x] добавить общий station UI manifest contract;
- [x] добавить отдельный target KTMA delivery UI;
- [x] добавить target UBSI product UI module;
- [x] desktop compile-time композитит KTMA + UBSI;
- [x] добавить contract-test, подтверждающий включение УБСИ в КТМА;
- [x] configure/build и новый `ktma.ubsi.ui_contract` проходят CI;
- [ ] полный CI пока блокирует унаследованный от base-ветки
  `desktop.test_page_tu_runtime`: та же ошибка есть на base commit `55a449f`.

## Этап 2 — физическое владение UBSI UI

- [ ] перенести `ubsi_ui_model`, measurement views, TU flow и реализацию TestPage
  за target `ktma_ubsi_ui`;
- [ ] оставить в desktop core только действительно общие виджеты станции;
- [ ] сохранить frozen Production/TУ UX и acceptance tests;
- [ ] UBSI UI получает данные через RunEvent/ScenarioRunResult и не управляет
  алгоритмом испытания напрямую.

## Этап 3 — station administration

- [ ] заменить YAML-specific entry point на `ArtifactRegistry`;
- [ ] ввести `DocumentProvider`: load/save/validate/syntax metadata;
- [ ] первым зарегистрировать YAML без изменения семантики текущих сценариев;
- [ ] добавить JSON, INI и TXT providers;
- [ ] TOML добавлять только вместе с первым реальным TOML consumer;
- [ ] разрешить delivery/product typed editors, сохранив raw-text fallback.

## Этап 4 — scenario/runtime providers

- [ ] ввести `ScenarioProvider` над общим run/evidence lifecycle;
- [ ] оставить YAML ScenarioEngine provider №1;
- [ ] перенести process execution runtime из `universal_miltechstation`;
- [ ] подключить существующую Python-программу внешним процессом;
- [ ] добавить optional Lua 5.4 provider для ППБ и разрешать его только
  в PPB composition.

## Этап 5 — завершение KTMA delivery

- [ ] интегрировать stateful ISD provider из `isd_driver_safe`;
- [ ] переносить StationSession/component/resource model маленькими проверяемыми
  срезами;
- [ ] КТМА владеет stand profile, registrar, equipment readiness и регистрацией
  всех своих product modules;
- [ ] оставить явные product slots для следующих частей КТМА кроме УБСИ.

## Этап 6 — убрать переходное наследование

- [ ] заменить hardware/product-доступ через `KtmaMainWindow : MainWindow`
  композицией;
- [ ] сократить и удалить защищённые `integration*()` escape hatches;
- [ ] station desktop shell получает contributions через contracts;
- [ ] в reusable station UI нет `if (ktma)` / `if (ubsi)`.

## Критерий архитектуры

Вторая реальная поставка/изделие — ППБ — должна подключаться без копирования
MainWindow и без PPB-ветвлений в station core.
ППБ сохраняет свой operator UI, может включать Lua и переиспользует только те
части платформы, которые реально общие.
