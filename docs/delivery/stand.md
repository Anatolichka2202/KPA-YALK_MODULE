# Стенд КТМА

Короткая карта реального стенда.

Детали конкретного протокола находятся в `docs/reference/protocols/`.

---

# Сеть

Оборудование:

```text
ПЭВМ стенда:
    192.168.0.50/24

RS-485 / ROKT adapter:
    192.168.0.115:1113

ИСД:
    192.168.0.101:80
```

UDP приём:

```text
bind 0.0.0.0:1113
filter sender 192.168.0.115:1113
```

---

# В7

Текущий подтверждённый VISA resource:

```text
USB0::0x164E::0x0DAD::TW00053184::INSTR
```

Конкретные VISA/COM/IP настройки принадлежат профилю стенда:

```text
data/profiles/stand_ktma.yaml
```

Их не надо дублировать в сценариях.

---

# Capability

Сценарий работает с возможностями, а не с моделью прибора:

```text
ulk.parameter_source
stand.switch_matrix
measure.reference_voltage
power.dc_supply
signal.generator
...
```

Профиль связывает capability с конкретным устройством.

---

# Код

```text
stand_core
    ScenarioEngine и общие типы

stand/src/ubsi_procedures.cpp
    предметные процедуры УБСИ

stand/adapters
    transport

stand/plugins
    equipment DLL

stand/src/catalog.cpp
    каталог

stand/src/run_store.cpp
    run data

stand/src/report_writer.cpp
    отчёт

desktop_orbita
    Qt UI

registrar
    жизненный цикл
```

---

# Питание и safe stop

Приборы и адаптер проверяются в порядке, соответствующем реальному питанию
стенда.

Любой:

```text
timeout
exception
cancel
stop
```

должен приводить к попытке:

```text
safeStopAll()
```

Активные выходы, генератор и питание должны быть безопасно сняты.

---

# Не искать сеть заново

Если задача не является сетевой диагностикой, агент не должен:

- менять адреса стенда;
- создавать bridge;
- создавать ad-hoc/P2P сеть;
- поднимать SMB-share;
- менять firewall;
- включать Internet Connection Sharing;
- перестраивать маршруты.

Для переноса выпуска существует отдельный подтверждённый SSH/SCP flow:

[deployment.md](deployment.md)
