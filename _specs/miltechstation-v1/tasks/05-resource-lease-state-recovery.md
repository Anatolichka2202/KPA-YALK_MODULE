# 05: Lease, состояния ресурсов и recovery

**Статус:** in-progress

**Блокируется:** 04: Зелёный resource-routing contract.

**Закрывает критерии:** К6.

## Что построить

Если один run занял общий ресурс, второй пересекающийся Free, ТУ или
Production run не стартует и оператор видит владельца/причину. Runtime ведёт
явное состояние ресурса READY, ACTIVE, SAFE, ERROR или INDETERMINATE. Timeout
или не подтверждённый cleanup не превращаются в «готово»; восстановление
выполняется только через определённый recovery flow и оставляет trace.

## Выполненный срез (2026-09-28)

- `ScenarioEngine::run()` принимает optional `ResourceLeaseManager`;
- перед первым физическим шагом он рекурсивно собирает все явно объявленные
  `ScenarioNode::requiredResources` и атомарно захватывает их;
- lease живёт до возврата `ScenarioEngine::run()`, включая его безусловный
  `safeStopAll()`;
- конфликт не запускает процедуру и возвращает `Incomplete` с одним
  `RESOURCE_LEASE`-событием с `resource` и владельцем `owner`;
- Main, Universal Free и КТМА передают manager текущего `StationSession` в
  common run entry point;
- `stand.scenario_resource` подтверждает conflict, delivery причины в
  `progressSink`, отсутствие воздействия при конфликте и release после run.

Это не закрывает задачу: `safeStopAll()` пока имеет тип `void noexcept`,
поэтому runtime не может доказательно записать `SAFE`. READY/ACTIVE/SAFE/
ERROR/INDETERMINATE, подтверждённый recovery и bench/Evidence gates остаются
незакрытыми.

## Критерии приёмки

- [x] Пересекающиеся runs не могут одновременно получить lease одного ресурса,
  если ресурс явно объявлен в сценарии.
- [x] Независимые runs с непересекающимися явно объявленными ресурсами не
  блокируют друг друга без причины.
- [ ] Timeout переводит ресурс в ERROR или INDETERMINATE с сохранённой
  причиной, а не в successful verdict.
- [ ] Safe-stop и recovery меняют состояние только после подтверждённого
  результата операции.
- [ ] Есть contract/integration tests на conflict, timeout, cleanup и partial
  failure. Сейчас есть contract-test на conflict/release.
- [ ] Для hardware-impacting реализации назначены bench/Evidence gates до
  статуса `done`.
