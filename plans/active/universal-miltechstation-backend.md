# Universal MilTechStation — backend refactor

## Цель

Отделить общую платформу MilTechStation от оборудования и логики конкретной
поставки. Состав станции должен задаваться профилем поставки. Протокольные
библиотеки, включая `liborbita`, не должны выбирать или владеть конкретным
физическим оборудованием станции.

Платформа должна в дальнейшем позволять подключать:

- физическое измерительное и управляющее оборудование;
- разные источники сырых потоков данных;
- существующие программы стендов как отдельные процессы;
- Lua/Python runtime;
- serial/SSH и другие средства взаимодействия с вычислительными платами.

## Зафиксированная архитектурная граница

```text
поставка
  ↓
StandProfile / components
  ↓
ComponentRuntime
  ├─ equipment
  ├─ sample_source
  └─ execution_runtime
       ↓
конкретные providers
```

Протокольный consumer находится ниже только по потоку данных:

```text
sample_source provider
  ↓ raw samples
liborbita
  ↓ decoded Orbita values
```

`liborbita` не должна выбирать E20-10 и не должна зависеть от Lusbapi после
завершения миграции desktop.

## Этапы

### 1. Декларативный состав станции

Статус: **DONE**

- [x] `ComponentProfile` добавлен в `StandProfile`;
- [x] секция `components:` загружается из профиля;
- [x] KTMA декларирует E20-10 как `kind=sample_source`, provider
  `miltech.sample.e2010`;
- [x] KTMA переведён на единую декларацию оборудования через
  `components:` / `kind=equipment`;
- [x] `devices:` больше не является вторым источником истины для KTMA;
- [x] legacy `devices:` остаётся входным форматом для старых поставок;
- [x] для существующего desktop загрузчик автоматически строит
  `StandProfile::devices` из canonical equipment components;
- [x] двойная декларация одного id одновременно в `components:` и `devices:`
  отвергается;
- [x] contract test проверяет реальный профиль КТМА и compatibility view.

### 2. Общий lifecycle компонентов

Статус: **DONE / FIRST VERSION**

- [x] `IStationComponent`;
- [x] `ComponentRuntime`;
- [x] kind factories;
- [x] выбор компонентов по id/binding;
- [x] выборочная инстанциация kinds для постепенной миграции;
- [x] запрет неоднозначных bindings;
- [x] best-effort `safeStopAll()`;
- [x] rollback при ошибке инстанциации;
- [x] contract tests.

### 3. Источники сырых отсчётов

Статус: **PARTIAL**

- [x] общий `ISampleSource` на уровне station;
- [x] E20-10 реализован как station provider `miltech.sample.e2010`;
- [x] `liborbita::pushSamples()` добавлен как независимый вход;
- [x] `sample_source` подключён к `ComponentRuntime`;
- [ ] desktop должен брать `telemetry.orbita.sample_source` из runtime;
- [ ] удалить legacy `setDeviceE2010()/setDeviceNone()` из liborbita;
- [ ] удалить `orbita/device/e2010_device.*` и старый `ISampleSource`;
- [ ] удалить зависимость liborbita от Lusbapi.

### 4. Запуск существующего стендового ПО

Статус: **FIRST VERSION**

- [x] общий `IExecutionRuntime`;
- [x] `ExecutionRequest` / `ExecutionResult`;
- [x] provider `miltech.exec.process` через `QProcess`;
- [x] working directory / env / args / stdout / stderr;
- [x] timeout / cancel / terminate / kill fallback;
- [x] factory contract test;
- [ ] интеграционный тест реального запуска процесса;
- [ ] связать execution runtime с run lifecycle/evidence;
- [ ] добавить Python provider/профиль после подключения первого реального Python-стенда;
- [ ] добавить Lua provider/adapter при миграции PPB;
- [ ] embedded runtime добавлять только если external-process integration окажется недостаточной.

### 5. Границы домена

Статус: **TODO**

- [ ] убрать `ubsi_*` и `yalk_*` из общего station domain target;
- [ ] перенести KTMA/UBSI procedures в delivery/object package;
- [ ] определить место общих процедур протокола «Орбита» вне core станции;
- [ ] KTMA-specific ROKT/ULK/ISD adapters не должны считаться общей частью платформы.

### 6. Resource/role model оборудования

Статус: **TODO / NEXT BACKEND BOUNDARY**

Канонический профиль уже описывает component instance отдельно от provider.
Но для `kind=equipment` поле `bind` пока содержит capability-id ради
совместимости с `EquipmentRegistry` и существующими сценариями.

Текущий `EquipmentRegistry` адресует устройство capability-id и поэтому не
может корректно представить два устройства с одной capability в разных ролях.
Нужно перейти к модели:

```text
logical resource/role
    ↓
конкретный component instance
    ↓
capabilities
```

При этом сценарий обращается к роли, а runtime отдельно валидирует capability.
Это нужно сделать до переноса PPB и других стендов, где одинаковые типы
приборов могут использоваться в разных назначениях.

### 7. Serial / SSH / board debugging

Статус: **TODO**

Не вводить одну искусственную универсальную «команду платы». Нужны отдельные
station-level contracts для транспорта/remote session, когда появится первый
реальный consumer:

- serial/raw console;
- SSH command/session/file transfer;
- при необходимости специализированный debug transport.

Физические параметры и credentials принадлежат профилю поставки/окружению, а
не общему продукту.

### 8. Desktop / application composition

Статус: **TODO**

После backend boundaries:

- [ ] убрать lifecycle оборудования из `MainWindow`;
- [ ] вынести station/application session;
- [ ] product/delivery package подключать композицией, а не разрастанием
  `integration*()` protected API;
- [ ] сохранить существующую операторскую модель УБСИ без нового redesign.

## Проверка

Для ветки создан отдельный GitHub Actions workflow:

```text
.github/workflows/universal-miltechstation-ci.yml
```

Он собирает проект на Windows UCRT64 / Qt 6 / GCC и запускает CTest при каждом
push в `universal_miltechstation`.

Текущий результат CI фиксировать здесь только после фактического завершения
workflow.

## Текущий следующий шаг

1. держать ветку зелёной через отдельный CI после каждого backend-среза;
2. переключить desktop Orbita monitoring на station-owned `sample_source`;
3. после этого физически удалить E20/Lusbapi из `liborbita`;
4. в backend отделить logical resource/role от capability;
5. затем начать вынос UBSI/KTMA domain из общего `station` target.
