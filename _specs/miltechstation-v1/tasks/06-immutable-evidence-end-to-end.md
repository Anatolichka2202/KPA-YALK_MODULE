# 06: Сквозной immutable Evidence

**Статус:** todo

**Блокируется:** 01: Project package и workflow identity; 03: External process в общем run; 04: Зелёный resource-routing contract.

**Закрывает критерии:** К7.

## Что построить

После одного generic run инженер открывает сохранённый факт исполнения и
видит неизменяемый context, упорядоченные command/ack/error/safety events,
measurements, verdict и ссылки на raw artifacts. Отчёт строится из этого
сохранённого результата, а не из временного состояния UI или предметной
процедуры.

## Критерии приёмки

- [ ] Run сохраняет project, workflow, configuration identity, DUT, operator и
  environment при их наличии.
- [ ] Equipment boundary записывает COMMAND, COMMAND_ACK, ERROR и SAFETY в
  одном упорядоченном event envelope.
- [ ] Measurement evidence содержит resource/device/quality identity либо
  явный статус отсутствия этих данных.
- [ ] stdout/stderr, telemetry и waveform могут быть приложены raw artifact
  metadata без записи каждого sample как generic event.
- [ ] Сохранённый run повторно отображается и формирует тот же verdict/report
  view.
- [ ] Есть integration test одного законченного generic run и его persistence/
  re-render.
