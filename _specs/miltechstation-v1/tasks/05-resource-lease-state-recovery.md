# 05: Lease, состояния ресурсов и recovery

**Статус:** done

**Блокируется:** 04: Зелёный resource-routing contract.

**Закрывает критерии:** К6.

## Что построить

Если один run занял общий ресурс, второй пересекающийся Free, ТУ или
Production run не стартует и оператор видит владельца/причину. Runtime ведёт
явное состояние ресурса READY, ACTIVE, SAFE, ERROR или INDETERMINATE. Timeout
или не подтверждённый cleanup не превращаются в «готово»; восстановление
выполняется только через определённый recovery flow и оставляет trace.

## Реализация (2026-09-28)

- `ScenarioEngine::run()` принимает optional `ResourceLeaseManager`;
- перед первым физическим шагом он рекурсивно собирает все явно объявленные
  `ScenarioNode::requiredResources` и атомарно захватывает их;
- lease живёт до возврата `ScenarioEngine::run()`, включая safety-stop;
- конфликт не запускает процедуру и возвращает `Incomplete` с одним
  `RESOURCE_LEASE`-событием с `resource` и владельцем `owner`;
- Main, Universal Free и КТМА передают manager текущего `StationSession` в
  common run entry point;
- `ResourceLeaseManager` ведёт READY/ACTIVE/SAFE/ERROR/INDETERMINATE,
  блокирует новый lease из ERROR/INDETERMINATE и сохраняет переходы в trace;
- `ICapabilityProvider::safeStopResources()` — additive confirmation seam;
  legacy `safeStopAll()` вызывается, но его отсутствие подтверждения даёт
  INDETERMINATE, а не SAFE;
- успешное выполнение с неподтверждённой остановкой возвращает `Error`;
- явный `recover(resource, evidenceId, operation)` оставляет ресурс
  заблокированным во время операции и переводит в READY только при `true`;
  исключение/отказ остаётся INDETERMINATE;
- `ScenarioEngine` пишет `RESOURCE_SAFE_STOP` / `RESOURCE_RECOVERY` с
  ресурсом, состоянием и причиной;
- `stand.scenario_resource` покрывает конфликт, отсутствие воздействия,
  подтверждённый cleanup, timeout/unconfirmed cleanup, блокировку повторного
  запуска, неудачное и успешное recovery.

**Граница подтверждения:** legacy equipment/plugin API всё ещё предоставляет
только `void safe_stop`; такие драйверы корректно классифицируются как
неподтверждённые. Подтверждённый production safe-stop должен быть подключён
через новый seam конкретного provider/plugin. Физический стендовый gate для
каждого аппаратного driver остаётся отдельным перед выпуском.

## Критерии приёмки

- [x] Пересекающиеся runs не могут одновременно получить lease одного ресурса,
  если ресурс явно объявлен в сценарии.
- [x] Независимые runs с непересекающимися явно объявленными ресурсами не
  блокируют друг друга без причины.
- [x] Timeout переводит ресурс в ERROR или INDETERMINATE с сохранённой
  причиной, а не в successful verdict.
- [x] Safe-stop и recovery меняют состояние только после подтверждённого
  результата операции.
- [x] Есть contract/integration tests на conflict, timeout, cleanup и partial
  failure.
- [x] Для hardware-impacting реализации назначены bench/Evidence gates:
  production-плагины должны сообщать подтверждение stop, после чего требуется
  стендовый запуск с сохранённым `RESOURCE_SAFE_STOP` Evidence. Gate назначен,
  но аппаратное подтверждение не заявляется этой программной задачей.

## Проверка

- Release build: `build/Desktop_Qt_6_8_0_MinGW_64_bit-Release` — успешно.
- `ctest --test-dir build/Desktop_Qt_6_8_0_MinGW_64_bit-Release
  --output-on-failure --timeout 60` — 24/24 успешно.
- `git diff --check` — успешно; Git вывел только предупреждения о нормализации
  CRLF.
