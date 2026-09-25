# УБСИ по ТУ на MilTechStation — план сходимости

## Цель

Главная текущая цель `master`:

> выполнить проверку УБСИ `ЛВРМ.468157.002` по ТУ `ЛВРМ.468157.002ТУ` на общей платформе MilTechStation без переноса TU-minimal HMI и без привязки station core к одному изделию.

`rebuild/tu-minimal-clean` остаётся замороженным mini-production/reference. Код в этой ветке не изменяется. Из неё в `master` переносится только подтверждённое поведение физического тракта и обработка стендовых отказов, причём в архитектуре MilTechStation.

Frozen donor baseline текущей работы:

```text
rebuild/tu-minimal-clean
e4ca10616e7d85f8ac9a0535482612a6d4d8f20b
```

Канонический task-level closure contract:

[../../docs/task/ubsi/traceability.md](../../docs/task/ubsi/traceability.md)

## Нормативная граница

Источник требований — предоставленный проект `ЛВРМ.468157.002ТУ`.

Пункт 5.6 прямо задаёт проверку функционирования УБСИ на соответствие требованиям:

`1.1.4.1–1.1.4.3`, `1.1.4.5–1.1.4.11`, `1.1.4.13`, `1.1.4.14`.

Именно этот набор является нормативным scope текущего сценария 5.6. Пункт `1.1.4.4` и сопротивление изоляции `1.1.4.12` относятся к отдельному методу 5.5. Пункт `1.1.4.15` проверяется в процессе изготовления по 5.2 и не должен попадать в автоматический verdict 5.6.

Важно: `1.1.4.6` (длина соединительной линии «термодатчик – УБСИ» до 50 м) прямо входит в scope 5.6. В предоставленном тексте 5.6 отдельный способ автоматической проверки этой длины не раскрыт. До появления подтверждённой методики этот пункт нельзя молча исключать и нельзя автоматически считать выполненным; он остаётся явным блокирующим gap для полного нормативного `OK`.

## Неподвижные инварианты

1. `master` остаётся универсальной платформой: KTMA/УБСИ — delivery/product layer.
2. UI `master` сохраняется; TU-minimal UI не переносится.
3. Сценарий обращается к логическим `resource + capability`, а не к модели прибора и не к HTTP/SCPI-командам.
4. Ошибка стенда/транспорта не является браком УБСИ.
5. `manual`, `deferred`, `skipped`, отсутствие оборудования или неподтверждённый физический маршрут не могут стать автоматическим `OK`.
6. Любая активная операция имеет определённый targeted safe-stop. Глобальный reset ИСД не используется как generic cleanup.
7. При неоднозначном результате активной HTTP-команды ИСД состояние считается неопределённым до адресного cleanup или явного recovery.
8. Приёмочный `OK` возможен только после завершения обязательного объёма и сохранения измерительного следа.
9. Если текст ТУ включает требование в scope, отсутствие подтверждённой аппаратной методики является явным `INCOMPLETE`/blocking gap, а не основанием удалить требование из acceptance.
10. Для каждого аппаратно-значимого пути УБСИ обязательна трассировка на соответствующий physical path frozen donor; donor является reference поведения, но не нормативным источником.
11. Изменение routing, active command order, settle/freshness, acceptance criterion или cleanup снимает `BENCH_VERIFIED` для затронутого пути до нового живого прогона текущего `master`.
12. Зелёный CI обязателен для `CLOSED`, но не заменяет donor trace, bench verification и Evidence verification.

## Definition of Closed для физического пути УБСИ

Путь считается `CLOSED` только если выполнена вся цепочка:

```text
TU requirement identified
        ↓
master IMPLEMENTED
        ↓
AUTO_TESTED
        ↓
DONOR_TRACED @ e4ca106...
        ↓
GREEN MASTER CI
        ↓
BENCH_VERIFIED on current master
        ↓
EVIDENCE_VERIFIED
        ↓
CLOSED
```

Если frozen donor не содержит эквивалентного пути, используется только явный `DONOR_NA` с обоснованием.

`SAME` означает сохранение физической семантики donor. `INTENTIONAL_DIFF` означает сознательное отличие master, для которого записана причина и получено собственное доказательство. Копирование timing/tolerance из donor без подтверждения запрещено.

- [x] создать канонический closure/traceability документ `docs/task/ubsi/traceability.md`;
- [x] зафиксировать frozen donor baseline SHA;
- [ ] для каждого mandatory physical path заполнить master scenario/procedure/resources;
- [ ] для каждого mandatory physical path заполнить donor files/probes/evidence;
- [ ] для каждого path выставить `SAME` / `INTENTIONAL_DIFF` / `DONOR_NA`;
- [ ] связать строку с конкретным automated test;
- [ ] связать строку с live master bench evidence;
- [ ] связать строку с Run/Evidence/report representation;
- [ ] запретить закрытие строки при красном обязательном CI.

## P0 — физический тракт УБСИ

### ИСД

2026-09-25: в `master` вместо выполнявшегося на каждом run `type=4`
реализован адресный startup baseline по frozen donor (`type=3 1–88,95–96`,
`type=2 1–40`, `type=1 1–88`, gap 30 мс). Подтверждение сохраняется в
экземпляре ИСД на время работы приложения; безопасный targeted cleanup
остаётся обязательным после каждого run. Контрактные тесты драйвера и
StationSession проходят. Новый physical path ещё **не BENCH_VERIFIED**.

- [x] убрать `type=4` из активной процедуры `ubsi.isd_baseline`;
- [x] сохранить экземпляр ИСД между проверками в одном запуске программы;
- [x] блокировать пропуск baseline при неопределённой сессии или оставленных маршрутах;
- [ ] проверить 218 OFF и последующий пропуск на живом `master` с сохранением Evidence;

- [x] stateful driver хранит process-owned desired state;
- [x] timeout/ошибка активной команды переводит сессию в `indeterminate`;
- [x] новые активные воздействия блокируются до cleanup/recovery;
- [x] `recover_after_restart` делает passive probe и адресно восстанавливает запомненное состояние без `type=4`;
- [x] `safe_stop` снимает только известные process-owned воздействия;
- [ ] завершить donor trace `master ISD driver/recovery -> donor hardware/isd_router.cpp + tests/isd_recovery_test.cpp`;
- [ ] подключить к KTMA HMI операторский recovery flow: пауза -> физический перезапуск ИСД -> `recover_after_restart` -> продолжение либо Stop;
- [ ] recovery и отказ оператора покрыть интеграционным тестом на уровне station/delivery;
- [ ] события recovery и trace ИСД сохранить в run evidence;
- [ ] выполнить bench recovery текущего master и сохранить evidence.

### ЯЛК

#### Исходное состояние / обрыв

Текущий status: `IMPLEMENTED`, но **не CLOSED**: обязательный CI на текущем master красный на двух open-input regressions.

- [x] выделить физический master path в `yalk_initial_physical.cpp`;
- [x] добавить targeted regression `yalk_initial_physical_test.cpp`;
- [ ] привести fixture/runtime к одному нормативному критерию обрыва `UяЛК < 0 В`, без ложной зависимости verdict от contact bit;
- [ ] получить зелёный CI;
- [ ] завершить donor trace через `procedures/yalk_procedures.cpp`, `procedures/yalk_initial_verdict.h`, `data/ubsi_yalk_trace.yaml`;
- [ ] выполнить current-master bench run;
- [ ] подтвердить evidence + targeted cleanup.

#### Analog / calibration / contacts / reference

- [ ] сверить `master` с frozen donor по фактической цепочке коммутации;
- [ ] для calibration сопоставить master с donor `yalk_procedures.cpp`, `yalk_verified_procedures.cpp`, `hardware/yalk_reference_link.cpp`;
- [ ] для contacts сопоставить verdict с `yalk_contact_verdict.h` и donor trace;
- [ ] для 6.2 В reference сопоставить master V7/reference path с `hardware/yalk_reference_link.cpp` и donor reference evidence;
- [ ] проверить для каждой измерительной точки цепочку `set -> settle -> fresh frame/reference204 -> independent reference -> verdict -> cleanup`;
- [ ] автоматическими тестами зафиксировать freshness и failure classification;
- [ ] выполнить live master points/sweep и сохранить Evidence.

#### Перегрузка ±12 В

В опубликованном TU NORMAL перегрузка перенесена после `yalk_initial`, до
аналоговых/контактных точек. Это изменение порядка не закрыто стендом.

Текущий status: `IMPLEMENTED + AUTO_TESTED + DONOR_TRACED`, но **не CLOSED**: нужен новый master bench/evidence и общий CI должен быть зелёным.

- [x] подтверждённые адреса `1–28, 32–43, 45–70, 74–87`;
- [x] `29–31`, `44`, `71–73`, `88` исключены из полного overload routing;
- [x] donor trace выполнен по `yalk_verified_procedures.cpp` и `ubsi_yalk_overload_*` scenario/probes;
- [x] background DAC и transient ±12 V разведены по ownership domains;
- [x] fresh snapshot anchor проверяется targeted test;
- [x] targeted cleanup не использует generic/global `type=4` reset;
- [ ] сделать explicit safe-address args в canonical master scenario вместо compatibility `*_count=88`;
- [ ] до/после перегрузки сохранить независимое измерительное свидетельство и свежесть в final Evidence;
- [ ] выполнить полный overload run текущего master;
- [ ] по живому прогону отдельно подтвердить/изменить master timing и delta criterion, не копируя donor автоматически;
- [ ] после bench run выставить parity `SAME` или `INTENTIONAL_DIFF` по каждому сознательно отличающемуся параметру.

### ЯТП / термосопротивления

- [ ] выполнить donor trace через `procedures/ytp_procedures.cpp` и соответствующий `data/ubsi_tu.yaml` path;
- [ ] подтвердить фактическую методику воздействия для диапазона `0–240 Ом`, четырёхпроводное подключение и не менее 30 каналов по `1.1.4.1`;
- [ ] точки сопротивления, settle и последовательность коммутации брать только из подтверждённой методики/рабочего тракта, не выводить из общих требований;
- [ ] после каждого воздействия принимать только свежий кадр УЛК;
- [ ] добавить/связать automated test с реальным scenario path;
- [ ] выполнить live master run и проверить Evidence;
- [ ] отдельно закрыть `1.1.4.6`: подтвердить способ доказательства линии «термодатчик – УБСИ» до 50 м; пока способ не подтверждён — не формировать полный нормативный `OK` 5.6.

### ЯВП

- [ ] выполнить полный donor trace через `procedures/yvp_procedures.cpp`, `procedures/yvp_verdict.h`, channel probes и donor evidence;
- [ ] не угадывать физическую карту;
- [ ] использовать только подтверждённый маршрут и подтверждённый источник измерения;
- [ ] проверить весь ряд коэффициентов `0,25; 0,5; 1; 2; 4; 8; 32 мВ/пКл` и требования АЧХ/затухания согласно `1.1.4.7–1.1.4.8` только после подтверждения физического тракта;
- [ ] устранить расхождение между scenario/traceability metadata и фактическим проверенным трактом;
- [ ] связать `yvp_v7_procedure_test` с конкретным physical path и failure semantics;
- [ ] выполнить live master bench run по подтверждённой commissioning схеме;
- [ ] проверить Evidence от stimulus до verdict/cleanup;
- [ ] если обязательный физический путь не подтверждён, результат должен быть `INCOMPLETE`, а не `OK`/`FAIL` изделия.

### Питание, готовность и общая безопасность

- [ ] выполнить donor trace через `procedures/power_procedures.cpp`, `hardware/akip1160.cpp`, startup probes/evidence;
- [ ] подтвердить порядок включения по 5.6.1–5.6.4 и рабочему reference;
- [ ] реализовать требования `1.1.4.2`, `1.1.4.3`, `1.1.4.5`, `1.1.4.13` только тем способом, который подтверждается схемой/оборудованием;
- [ ] не подменять ток питания датчиков из `1.1.4.2` общим током УБСИ из `1.1.4.5`;
- [ ] Stop/exception/закрытие приложения должны приводить все process-owned active outputs в safe state;
- [ ] safe-stop одного устройства не должен мешать попытке остановить остальные;
- [ ] выполнить power/readiness current-master bench run и проверить timestamps/freshness/evidence.

## P0 — трассируемость п. 5.6

Для каждого обязательного требования должна существовать однозначная строка:

```text
TU requirement
    -> scenario node / explicit unresolved gate
    -> procedure
    -> logical resources
    -> physical evidence
    -> donor parity decision
    -> automated test
    -> live master evidence
    -> acceptance rule
    -> result/evidence record
```

- [ ] привести `ubsi_tu_5_6.yaml`, вложенные сценарии и `ubsi_tu_5_6_traceability.csv` к точному scope 5.6;
- [ ] удалить `1.1.4.4`/`1.1.4.12` из семантики 5.6 и оставить их в отдельной трассировке метода 5.5;
- [ ] вернуть `1.1.4.6` в обязательную трассировку 5.6 как unresolved blocking requirement до подтверждения метода;
- [ ] контрактным тестом запретить исчезновение любого обязательного пункта п. 5.6;
- [ ] контрактным тестом запретить автоматический `OK` для manual/deferred/unresolved/unknown physical route;
- [ ] результат стендовой ошибки хранить отдельно от `FAIL` изделия;
- [ ] каждому физическому requirement/path назначить closure status из `docs/task/ubsi/traceability.md`;
- [ ] полный TU result не может стать `CLOSED`, пока хотя бы одна mandatory строка не `CLOSED` или формально не разрешена отдельной нормативной процедурой.

## P1 — платформа, необходимая для УБСИ

После закрытия физического P0:

- typed operation schema для capability/plugin operations;
- Scenario IR как единая модель YAML и будущего visual editor;
- schema-driven validation и property editor;
- run/evidence storage с версией scenario/profile/plugin;
- Engineering Studio без вмешательства в production Operator HMI;
- script/process integration только через Station resources и safety ownership.

Эти работы не должны блокировать восстановление рабочего УБСИ тракта.

## Проверка изменений

Минимум для каждого backend-среза:

1. unit/contract tests соответствующего слоя;
2. Windows UCRT64 configure/build/CTest через репозиторный CI;
3. отсутствие изменения `rebuild/tu-minimal-clean`;
4. review diff на отсутствие низкоуровневых KTMA/УБСИ деталей в generic station core;
5. для UBSI hardware slice — обязательный donor trace на frozen SHA;
6. для hardware-значимых изменений — отдельный current-master bench checklist/run;
7. проверка Run/Evidence и cleanup до выставления `CLOSED`.

CI и mock-тесты подтверждают программный контракт, но **не подтверждают соответствие УБСИ ТУ на физическом стенде**. Донор подтверждает прежний физический path, но **не подтверждает текущий master**. Финальная отметка возможна только после фактического прогона соответствующего пути на текущем `master` с сохранённым Evidence.
