# 06: Сквозной immutable Evidence

**Статус:** in-progress

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
- [ ] Measurement evidence содержит resource/device/quality identity либо
  явный статус отсутствия этих данных.
- [ ] stdout/stderr, telemetry и waveform могут быть приложены raw artifact
  metadata без записи каждого sample как generic event.
- [x] Сохранённый run повторно отображается и формирует тот же verdict/report
  view.
- [x] Есть integration test одного законченного generic run и его persistence/
  re-render.

## Фактическое состояние

`RunStore::load()` восстанавливает `ScenarioRunResult` из SQLite: identity,
steps/measurements, legacy events и structured evidence. `scenario_title`
сохраняется additive migration, поэтому renderer не вынужден подменять его
идентификатором сценария. Контракт `stand.run_store_context` выполняет generic
run через `runScenarioWithEvidence()`, сохраняет его, читает обратно и строит
HTML-отчёт из восстановленной модели.

Это не закрывает задачу: `RunArtifacts` пока создаёт файлы telemetry/raw
отдельно от `ScenarioRunResult`, а measurement evidence не содержит обязательной
resource/device/quality identity. До связывания этих частей с сохранённым run
критерий К7 остаётся незакрытым.
