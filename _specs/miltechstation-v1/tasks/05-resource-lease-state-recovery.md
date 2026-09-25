# 05: Lease, состояния ресурсов и recovery

**Статус:** todo

**Блокируется:** 04: Зелёный resource-routing contract.

**Закрывает критерии:** К6.

## Что построить

Если один run занял общий ресурс, второй пересекающийся Free, ТУ или
Production run не стартует и оператор видит владельца/причину. Runtime ведёт
явное состояние ресурса READY, ACTIVE, SAFE, ERROR или INDETERMINATE. Timeout
или не подтверждённый cleanup не превращаются в «готово»; восстановление
выполняется только через определённый recovery flow и оставляет trace.

## Критерии приёмки

- [ ] Пересекающиеся runs не могут одновременно получить lease одного ресурса.
- [ ] Независимые runs с непересекающимися ресурсами не блокируют друг друга
  без причины.
- [ ] Timeout переводит ресурс в ERROR или INDETERMINATE с сохранённой
  причиной, а не в successful verdict.
- [ ] Safe-stop и recovery меняют состояние только после подтверждённого
  результата операции.
- [ ] Есть contract/integration tests на conflict, timeout, cleanup и partial
  failure.
- [ ] Для hardware-impacting реализации назначены bench/Evidence gates до
  статуса `done`.
