# Перестройка документации

## Цель

Сократить документацию репозитория в разы и построить единственную
каноническую систему знаний с минимальным стартовым контекстом.

## Целевая модель

```text
корень:
  README.md
  AGENTS.md
  PROJECT_MEMORY.md

документация:
  docs/

динамические планы:
  plans/
```

Документационные материалы вне этих областей после миграции запрещены.

---

## Этап 1 — границы и новый вход

Статус: **DONE**

Выполнено:

- создан короткий корневой `README.md`;
- `AGENTS.md` заменён правилами новой документационной системы;
- создан `docs/INDEX.md`;
- зафиксированы границы:
  - репозиторий;
  - продукт MilTechStation;
  - поставка КТМА;
  - текущий объект УБСИ;
- создан базовый документ продукта;
- создан базовый документ поставки;
- создан базовый документ текущего объекта;
- `docs/` настроен как Obsidian vault;
- создана новая директория `plans/`.

Старые документы пока намеренно не удалены.

Они нужны как сырьё следующих этапов миграции.

---

## Этап 2 — рабочая память и исследования

Статус: **DONE**

Выполнено:

- `PROJECT_MEMORY.md` полностью пересобран;
- memory сокращена до актуальной рабочей правды;
- старые UI-прототипы исключены из источников дизайна;
- опровергнутые ROKT/ЯТП/ЯВП/overload-гипотезы не перенесены;
- зафиксирована граница MilTechStation → КТМА → УБСИ;
- создан `docs/research/INDEX.md`;
- создан `docs/research/reference-programs.md`;
- создан `docs/research/stand.md`;
- KPA_Rokot перенесён в research/evidence;
- текстовые извлечения из KPA_Rokot перенесены в research/evidence;
- подтверждённые стендовые результаты отделены от канонической
  protocol-документации;
- `docs/INDEX.md` дополнен маршрутом к исследованиям.

В research не переносилась полная историческая хроника старого
`08_ЖУРНАЛ_СТЕНДА.md`.

Опровергнутые промежуточные выводы остаются только в Git history.

---

## Этап 3 — протоколы, API, ABI, компоненты

Статус: **DONE**

Перед генерацией документации проверены актуальные контракты в коде.

Создано:

```text
docs/reference/INDEX.md

docs/reference/api.md
docs/reference/abi.md

docs/reference/protocols/INDEX.md
docs/reference/protocols/orbita.md
docs/reference/protocols/rokt.md
docs/reference/protocols/scpi-visa.md
docs/reference/protocols/isd-http.md

docs/reference/components/INDEX.md
docs/reference/components/liborbita.md
docs/reference/components/equipment-plugins.md
```

Зафиксированы границы:

```text
API
    программный вызов

ABI
    бинарная граница плагина

protocol
    обмен с внешним оборудованием

component
    краткая роль программного компонента
```

В новый protocol-reference не перенесены старые опровергнутые
варианты запуска ЯТП и неподтверждённые команды.

`docs/api_contract.md` пока физически остаётся в старой документации,
но больше не является канонической точкой входа.

Локальные README компонентов пока не удалены — это будет сделано на
финальном cleanup после миграции остальных уникальных сведений.

---

## Этап 4 — данные и администрирование

Статус: **DONE**

Перед записью документации проверена текущая реализация:

```text
stand/src/catalog.cpp
stand/src/run_store.cpp
stand/src/report_writer.cpp

registrar/src/registrar.cpp
registrar/src/report.cpp

ktma/ubsi/src/production_ledger.cpp
ktma/ubsi/src/production_report.cpp
```

Создано:

```text
docs/product/data.md

docs/delivery/data.md
docs/delivery/reporting.md
docs/delivery/administration.md

docs/task/ubsi/production-data.md
```

Зафиксировано разделение:

```text
parameters.db
    каталог / metadata

runs.db
    фактические ScenarioEngine runs

registrar.db
    изделие / состав / замены / этапы / run_id
```

Также зафиксировано, что task-specific ProductionLedger УБСИ хранит
snapshot состава и production lifecycle отдельно от общего RunStore.

В новый канон НЕ перенесено устаревшее утверждение о том, что прежние
четыре электрических этапа автоматически дают итог изделия `OK`.

Текущий `Registrar::productVerdict()` сознательно возвращает
`Incomplete` до подключения актуальной verification policy.

Старые документы пока не удалены.

---

## Этап 5 — исходные данные УБСИ и ТУ

Статус: **DONE**

Зафиксировано, что первичные материалы находятся во внешнем локальном
архиве организации:

```text
C:\Users\МилТех\Desktop\Новая папка\docs\по ктма
```

Оригиналы не копируются в Git.

Создано:

```text
docs/sources/INDEX.md
docs/sources/external-source-inventory.json

docs/research/external-archive.md

docs/task/ubsi/overview.md
docs/task/ubsi/tu.md
docs/task/ubsi/tu-work.md
docs/task/ubsi/testing.md
```

Зафиксировано разделение:

```text
source
    что написано в исходном документе

tu.md
    нормативная выжимка

tu-work.md
    как текущий стенд закрывает требование

testing.md
    физическая методика
```

Не перенесены:

- старые UI-прототипы;
- опровергнутые ROKT-команды;
- автоматическая "догадка" `89..96` как доказанная карта ЯВП;
- выдуманный поканальный ток;
- старый внешний evidence-flow для исключённых пунктов;
- старые дизайнерские спецификации.

ЯВП оставлен `COMMISSIONING` до подтверждения измерительного payload и acceptance criterion.

Старые документы пока физически не удалены.

---

## Этап 5.1 — сборка, стенд, deployment и ЯВП

Статус: **DONE**

Добавлено:

```text
docs/product/build.md
docs/delivery/stand.md
docs/delivery/deployment.md
docs/task/ubsi/yvp.md
```

Зафиксировано:

- Qt Creator / существующий Kit — нормальный основной workflow разработки;
- агент не выбирает новый toolchain без прямой задачи;
- build artefacts не размазываются по repo;
- на стенд доставляется готовый release;
- Qt/CMake/compiler на стенде не нужны;
- обычный deployment идёт через существующий SSH/SCP;
- P2P/ad-hoc/SMB/FTP/VPN не создаются для обычного переноса;
- release directories не перезаписываются;
- hostname стенда проверяется перед удалёнными действиями;
- детальный статус ЯВП сохранён отдельным opt-in документом.

Актуальный ЯВП commissioning:

```text
Rigol DG-1022Z CH1
  -> вывод «З» ЯВП 8-2
  -> ЯВП-8
  -> adapter
  -> ROKT / UDP

ROKT 0A 01
ROKT 0A 03 channel 1..8
fresh UDP transport evidence
```

На текущем этапе Rigol должен быть доступен по VISA, а `1 В / OUTPUT` выставляются оператором вручную.

Автоматические `set_sine/output` заблокированы профилем до подтверждения трактовки амплитуды `Vpp/peak/RMS`.

Старая гипотеза `ЯВП -> обычный поток ЯЛК -> 88..96` не считается текущим acceptance path.

Открытые commissioning items ЯВП:

- frame layout полезного payload;
- raw -> физическая величина;
- связь payload с Uout / коэффициентом усиления / АЧХ;
- HTTP type/num штатной коммутации входов;
- код КУ1..КУ4;
- единица программной амплитуды Rigol;
- окончательный acceptance criterion;
- полный живой прогон после закрытия decoder;
- явный safe-off ручного `Rigol OUTPUT` после commissioning run.

До закрытия decoder процедура остаётся `INCOMPLETE`.

---

## Этап 6 — операторский интерфейс

Статус: **IN_PROGRESS**

Создан канонический документ:

```text
docs/task/ubsi/operator.md
```

Выполнено:

- операторский UI получил канонического владельца требований;
- `docs/INDEX.md` и `docs/task/ubsi.md` обновлены;
- старый `docs/operator-ui-new-dis.md` понижен до исторического non-canonical документа;
- операторское ТЗ отделено от конкретной реализации `new-dis`;
- зафиксирован единый persistent workspace;
- зафиксированы production scopes Full / ЯЛК / ЯТП / ЯВП;
- зафиксировано, что питание и ЯП-П не являются отдельными пользовательскими scope;
- зафиксированы требования к ЯЛК, ЯТП, ЯВП, Preparation и завершению;
- создан отдельный активный план `plans/active/ubsi-operator-ui.md`;
- синхронизированы противоречивые места `tu-work.md`, `testing.md` и `production-data.md` с текущим ЯВП commissioning.

Открытые несостыковки реализации:

1. `FullUbsi` включает `YP-P` в affected composition, но full scenario не имеет отдельного `YP-P` node — требуется доказать coverage или добавить backend-процедуру;
2. production session должна иметь явную очередь выбранных зарегистрированных УБСИ — текущее поведение требует проверки/доводки;
3. перегрузка ЯЛК требует полноценного обзора остальных каналов относительно baseline;
4. текущая `≈ total / remaining` оценка строится из процента progress и не является утверждённым HMI-требованием;
5. финальное состояние run требует визуальной доводки;
6. ЯВП остаётся commissioning и не может выдавать acceptance verdict;
7. ручной `Rigol OUTPUT` требует явно зафиксированного safe-off, поскольку текущий `yvp.safe_cleanup` не управляет генератором.

Подробный порядок закрытия:

[../../plans/active/ubsi-operator-ui.md](../../plans/active/ubsi-operator-ui.md)

Этап 6 переводится в `DONE` только после устранения документационных конфликтов и фиксации, что оставшиеся UI-gap либо реализованы, либо сознательно вынесены в отдельный действующий план с подтверждённым статусом. На текущий момент отдельный план создан, но ключевые функциональные расхождения ещё открыты.

---

## Этап 7 — очистка

Статус: TODO

После извлечения уникальных знаний:

- удалить старую структуру `docs/00...33`;
- удалить старый `docs/ktma/*`;
- удалить `_plans/`;
- удалить документацию старых UI-прототипов;
- удалить локальные README рядом с компонентами;
- удалить сгенерированные QA-документы и рендеры;
- перенести необходимые первичные документы в `docs/sources/`;
- перенести необходимые изображения в `docs/assets/`;
- проверить отсутствие проектной документации вне разрешённых областей;
- проверить все ссылки;
- обновить `docs/INDEX.md`;
- перенести этот plan в `plans/done/`.

На этапе cleanup удалить `docs/operator-ui-new-dis.md`, если больше нет ссылок, требующих его временного сохранения.

Git остаётся историей старых редакций. Отдельный архив старой канонической
документации не создаётся.
