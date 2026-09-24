# УБСИ — трассировка, тестируемость и закрытие

Этот документ определяет, когда физический путь проверки УБСИ считается действительно закрытым.

Он не заменяет:

- нормативные требования: [tu.md](tu.md);
- рабочую карту требований: [tu-work.md](tu-work.md);
- физическую методику: [testing.md](testing.md).

Он связывает их с кодом, тестами, живым стендом и замороженной референсной реализацией.

---

# 1. Приоритет источников

Для УБСИ используются разные источники с разной ролью.

```text
ТУ ЛВРМ.468157.002ТУ
    норматив: ЧТО должно быть доказано

master
    текущая реализация MilTechStation: КАК это выполняется сейчас

rebuild/tu-minimal-clean
    проверенный физический reference: КАК фактически работал подтверждённый
    mini-production тракт и какие edge cases уже были обнаружены

live bench evidence
    доказательство того, что текущий master действительно работает на железе
```

`rebuild/tu-minimal-clean` **не является нормативным источником** и не имеет права переопределять ТУ.

Он используется как обязательный reference/parity gate для физического пути УБСИ: перед закрытием пути должно быть явно установлено, что соответствующий участок donor изучен, а совпадения и осознанные отличия зафиксированы.

Если master намеренно отличается от donor, это допустимо только при явной причине и отдельном подтверждении тестом/живым прогоном.

---

# 2. Замороженный donor baseline

Ветка:

```text
rebuild/tu-minimal-clean
```

Зафиксированный baseline для текущей трассировки:

```text
e4ca10616e7d85f8ac9a0535482612a6d4d8f20b
```

Ветка read-only.

Правило направления изменений:

```text
TU / donor / live evidence
          ↓
        master
```

Обратного переноса изменений из `master` в `rebuild/tu-minimal-clean` для этой работы нет.

Если donor branch когда-либо будет сознательно переведён на другой baseline, SHA в этом документе обновляется отдельным решением. Молчаливое использование плавающего HEAD запрещено.

---

# 3. Обязательная цепочка трассировки физического пути

Для каждого аппаратно-значимого acceptance path УБСИ должна существовать проверяемая цепочка:

```text
TU requirement
    ↓
master scenario node / explicit unresolved gate
    ↓
master procedure
    ↓
logical resource + capability
    ↓
master plugin / transport / physical route
    ↓
donor reference @ frozen SHA
    ↓
parity decision: SAME / INTENTIONAL_DIFF / DONOR_NA
    ↓
automated contract/integration test
    ↓
live master bench evidence
    ↓
Run/Evidence + report trace
    ↓
acceptance result
```

Наличие кода только в одной точке этой цепочки не закрывает путь.

---

# 4. Статусы закрытия УБСИ

Для task-level tracking используются следующие статусы:

```text
OPEN
    путь ещё не разобран полностью

IMPLEMENTED
    путь существует в master

AUTO_TESTED
    основной контракт и существенные failure/safety cases покрыты автотестом

DONOR_TRACED
    соответствующий физический путь в frozen donor просмотрен;
    совпадения/различия master задокументированы

BENCH_VERIFIED
    текущий master фактически прошёл этот путь на живом стенде

EVIDENCE_VERIFIED
    Evidence текущего master доказывает воздействие, свежесть измерения,
    критерий, источник данных, результат и cleanup

CLOSED
    выполнены все обязательные gate и CI текущего master зелёный
```

Допустимо `DONOR_NA`, если в frozen donor реально нет эквивалентного пути. Причина должна быть записана в строке трассировки.

Для физического TU path УБСИ статус `CLOSED` требует одновременно:

```text
IMPLEMENTED
+ AUTO_TESTED
+ DONOR_TRACED или обоснованный DONOR_NA
+ BENCH_VERIFIED
+ EVIDENCE_VERIFIED
+ GREEN MASTER CI
```

---

# 5. Что означает DONOR_TRACED

`DONOR_TRACED` — не галочка «файл найден».

Минимально должны быть проверены:

- физическая адресация/маршрут;
- порядок активных команд;
- выдержки, если они влияют на результат;
- freshness / способ исключить старое измерение;
- источник эталонного значения;
- критерий результата;
- обработка timeout/ошибки;
- cleanup/safe state;
- известные исключения/edge cases.

Результат сравнения фиксируется как:

```text
SAME
    master сохраняет подтверждённую физическую семантику donor

INTENTIONAL_DIFF
    master сознательно отличается; причина и новое доказательство указаны

DONOR_NA
    эквивалентного пути в donor нет
```

Значения donor нельзя автоматически копировать в master только потому, что они работали раньше. Особенно это относится к timing, tolerance и неочевидным физическим критериям.

---

# 6. Запрет ложной закрытости

Следующие состояния **не являются CLOSED**:

- код с зелёным unit test без живого прогона аппаратно-значимого пути;
- живой прогон старой ветки donor вместо текущего master;
- `manual`, `deferred`, `skipped` или неподтверждённый route;
- test fixture, который не моделирует freshness/cleanup, когда они существенны;
- `OK`, полученный при отсутствии обязательного measurement/evidence;
- отсутствие результата из-за стенда, записанное как `FAIL` изделия;
- путь, который менялся после последнего bench evidence;
- локально прошедший тест при красном обязательном CI master.

Изменение физического routing, active command order, settle/freshness, acceptance criterion или cleanup автоматически снимает `BENCH_VERIFIED` для затронутого пути до нового прогона текущего master.

---

# 7. Обязательный набор доказательств на строку

Для закрываемого пути фиксируются:

```text
TU requirement / method
master commit
master scenario node
master procedure
resources/capabilities
master test name
frozen donor SHA
frozen donor paths
parity decision + note
bench run/evidence id or artifact
cleanup result
final closure status
```

Если какой-либо пункт не применим, указывается `N/A` и причина.

---

# 8. Текущая карта физического пути

Это рабочая карта, а не утверждение о полном закрытии УБСИ.

| Путь | Master | Frozen donor reference | Текущий статус |
|---|---|---|---|
| Питание / power cycle / готовность | current UBSI power/readiness procedures + project scenario | `procedures/power_procedures.cpp`, `hardware/akip1160.cpp`, `data/ubsi_tu.yaml`, startup probes/evidence | `OPEN: DONOR TRACE + MASTER BENCH REQUIRED` |
| ЯЛК исходное состояние / обрыв | `src/procedures/yalk_initial_physical.cpp`, `tests/yalk_initial_physical_test.cpp` | `procedures/yalk_procedures.cpp`, `procedures/yalk_initial_verdict.h`, `data/ubsi_yalk_trace.yaml` | `IMPLEMENTED; AUTO TEST CURRENTLY NOT GREEN; DONOR TRACE IN PROGRESS` |
| ЯЛК calibration / analog channels | `src/procedures/yalk_analog_procedure.cpp` + current procedures/scenario | `procedures/yalk_procedures.cpp`, `procedures/yalk_verified_procedures.cpp`, `hardware/yalk_reference_link.cpp`, `data/ubsi_yalk_trace.yaml` | `TRACE_REQUIRED` |
| ЯЛК contacts | current YALK procedures/scenario | `procedures/yalk_procedures.cpp`, `procedures/yalk_contact_verdict.h`, `data/ubsi_yalk_trace.yaml` | `TRACE_REQUIRED` |
| ЯЛК overload ±12 В | `src/procedures/yalk_overload_physical.cpp`, `tests/yalk_overload_physical_test.cpp` | `procedures/yalk_verified_procedures.cpp`, `data/ubsi_yalk_overload_trace.yaml`, `data/ubsi_yalk_overload_full_production.yaml`, point probes | `IMPLEMENTED + AUTO_TESTED + DONOR_TRACED; MASTER BENCH/EVIDENCE REQUIRED; NOT CLOSED WHILE CI RED` |
| ЯЛК 6.2 В reference | current V7/YALK procedures | `hardware/yalk_reference_link.cpp`, `docs/ubsi-uref-6v2-trace.md`, YALK procedures | `TRACE_REQUIRED` |
| ЯЛК targeted cleanup / ISD recovery | current ISD driver + YALK procedures | `hardware/isd_router.cpp`, `tests/isd_recovery_test.cpp` | `PARTIAL; TRACE + INTEGRATION + HMI RECOVERY REQUIRED` |
| ЯТП | current UBSI procedures/scenario | `procedures/ytp_procedures.cpp`, `data/ubsi_tu.yaml` | `TRACE_REQUIRED` |
| ЯВП | current V7/YVP procedures + commissioning | `procedures/yvp_procedures.cpp`, `procedures/yvp_verdict.h`, YVP channel probes, `docs/engineering/evidence/yvp_104_points_20260922.log` | `TRACE_REQUIRED; MASTER PHYSICAL PATH NOT CLOSED` |
| Финальный TU result / report | TU coverage gate + Run/Evidence/report | `tests/scenario_engine_test.cpp`, `tests/tu_report_test.cpp`, donor TU report path | `TRACE_REQUIRED; FULL MASTER RUN REQUIRED` |

`src/procedures/...` в таблице относится к `deliveries/ktma/ubsi/`, если не указано иначе.

Статус строки меняется только после проверки фактов. Старое evidence donor подтверждает donor, но не выдаёт `BENCH_VERIFIED` текущему master.

---

# 9. Текущий CI blocker

На момент введения этого документа текущий `master` после физического YALK initial slice собирается, но обязательный CI не зелёный: падают проверки, связанные с критерием обрыва ЯЛК.

Следовательно, даже те YALK slices, чей локальный targeted test проходит, сейчас не могут получить статус `CLOSED`.

После исправления blocker эта секция должна быть обновлена, а не оставаться историческим статусом.

---

# 10. Порядок работы с новым физическим slice

Перед кодом или сразу при входе в slice:

1. определить нормативное требование и ожидаемое evidence;
2. найти соответствующий donor path на frozen SHA;
3. выписать physical semantics и edge cases;
4. сравнить с текущим master;
5. реализовать/исправить master без копирования donor UI/архитектуры;
6. добавить automated contract/integration regression;
7. получить зелёный CI;
8. выполнить текущий master на живом стенде;
9. проверить Evidence и cleanup;
10. только после этого выставить `CLOSED`.

---

# 11. Связанные документы

- [УБСИ — точка входа](../ubsi.md)
- [Требования ТУ](tu.md)
- [Рабочая карта ТУ](tu-work.md)
- [Физическая методика](testing.md)
- [ЯВП commissioning](yvp.md)
- [План сходимости УБСИ на MilTechStation](../../../plans/active/ubsi-tu-on-miltechstation.md)
