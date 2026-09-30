# 06: Сквозной immutable Evidence

**Статус:** done

**Блокируется:** 01: Project package и workflow identity; 03: External process в общем run; 04: Зелёный resource-routing contract.

**Закрывает критерии:** К7.

## Что построить

После одного generic run инженер открывает сохранённый факт исполнения и
видит неизменяемый context, упорядоченные command/ack/error/safety events,
measurements, verdict и ссылки на raw artifacts. Отчёт строится из этого
сохранённого результата, а не из временного состояния UI или предметной
процедуры.

## Критерии приёмки

- [x] Run сохраняет project, workflow, configuration identity, DUT, operator и
  environment при их наличии.
- [x] Equipment boundary записывает COMMAND, COMMAND_ACK, ERROR и SAFETY в
  одном упорядоченном event envelope.
- [x] Measurement evidence содержит resource/device/quality identity либо
  явный статус отсутствия этих данных.
- [x] stdout/stderr, telemetry и waveform могут быть приложены raw artifact
  metadata без записи каждого sample как generic event.
- [x] Сохранённый run повторно отображается и формирует тот же verdict/report
  view.
- [x] Есть integration test одного законченного generic run и его persistence/
  re-render.

## Фактическое состояние

`RunStore::load()` восстанавливает `ScenarioRunResult` из SQLite: identity,
steps/measurements, legacy events и structured evidence. Каждое measurement
хранит `PROVIDED` resource/device/quality либо явный `NOT_PROVIDED`.
`scenario_title`
сохраняется additive migration, поэтому renderer не вынужден подменять его
идентификатором сценария. Контракт `stand.run_store_context` выполняет generic
run через `runScenarioWithEvidence()`, сохраняет его, читает обратно и строит
HTML-отчёт из восстановленной модели.

`RunArtifacts::attachTo()` прикладывает к run каталог, относительный путь,
размер и SHA-256 telemetry/raw packet файлов. Контракт не утверждает, что
файл физически невозможно изменить после запуска; hash позволяет обнаружить
расхождение с сохранённым фактом.

Все критерии этой task выполнены локальным integration contract. Её закрытие
не заменяет отдельные bench/Evidence gates аппаратно значимых поставок.
