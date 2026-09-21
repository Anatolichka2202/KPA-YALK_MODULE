# УБСИ: трассировка высокостабильного эталона 6,2 В

## Область документа

Этот документ относится только к пункту ТУ 1.1.4.9: высокостабильному
эталону `6,2 +/- 0,03 В`. Он не распространяется на калибровочные адреса
97/99, питание датчиков `6,2 +/- 0,2 В` и внешнее напряжение, формируемое
ЦАП ИСД.

## Подтверждённые факты

| Evidence | файл/лист | элемент | pin | net | куда идёт | статус |
|---|---|---|---|---|---|---|
| `reference204` имеет 100 little-endian 16-bit слов после 4-байтного заголовка; host хранит слово с external address `N` в vector index `N-1` | `hardware/yalk_reference_link.cpp`, `decodeYalkFrame()` и `yalkSnapshot()` | host decoder | не применимо | не применимо | external `reference204` | CONFIRMED |
| Прошивка выбирает аналоговый источник словом `MX_P1_CACSH[QUERY[1]]`, записывает его в `FIO1PIN` и считывает результат из `AD0DR0` | `tmp/electrical_sources/ktma/ЯЛК/main.cpp`, строки с `FIO1PIN`, `AD0CR`, `AD0DR0` | ЯЛК firmware | `AD0DR0` | не установлено | внутренний ADC result | CONFIRMED |
| Служебные internal indices 80..83 имеют слова `0x04010110`, `0x04010012`, `0x04010010`, `0x04000000` | `tmp/electrical_sources/ktma/ЯЛК/main.cpp`, таблица `MX_P1_CACSH` | ЯЛК firmware | не установлено | не установлено | четыре служебных выбора mux | CONFIRMED |
| Firmware контролирует code `88..168` для internal index 80 и `888..968` для internal index 82 | `tmp/electrical_sources/ktma/ЯЛК/main.cpp`, проверки `QueryTmp==80` и `QueryTmp==82` | ЯЛК firmware | не установлено | не установлено | `WorkFlag` | CONFIRMED |
| Host-калибровка использует external addresses 97 и 99; decoder test закрепляет positions 96 и 98 | `procedures/yalk_procedures.cpp`, `calibration()`; `C:/qt_repos_2/liborbita_master_probe/station/tests/equipment_protocol_test.cpp` | host calibration | не применимо | не применимо | external 97/99 | CONFIRMED |
| В старом CSV addr98 имеет raw `1939,5`, code `915,5`; это измерение не доказывает происхождение сигнала | `tmp/run-1789970458583-fcdb1ee8/TU_1789970458583-fcdb1ee8.csv`, строка `ubsi.reference_6v2.adapter` | historical run | не применимо | не установлено | external addr98 | CONFIRMED AS RAW OBSERVATION |

## Что не доказано

| Вопрос | Фактическая граница | Статус |
|---|---|---|
| `internal 80/81/82/83 -> external 97/98/99/100` | Host decoder распаковывает уже сформированный пакет, но среди доступных исходников не найден код адаптера/алгоритм упаковки, задающий это соответствие. Совпадение 80/82 с известными калибровочными кодами является только гипотезой. | HYPOTHESIS |
| Какой analog mux/net выбирает `0x04010012` | В доступном архиве ЯЛК есть firmware-проект (`ЯЛК_20_05_2026.zip`), но нет читаемой принципиальной схемы или netlist ЯЛК. | НЕ ПОДТВЕРЖДЕНО |
| Компонент, формирующий Uref 6,2 В | Не найдены refdes, маркировка, pins, вход/выход и цепочка резисторов источника. | НЕ ПОДТВЕРЖДЕНО |
| Внешняя измерительная точка Uref | Не найдено доказательство выхода Uref на разъём, кросс, В7 или ИСД. Net `+6.2V` на плате кросса не используется как доказательство: его назначение не установлено и он может относиться к питанию датчиков. | НЕ ПОДТВЕРЖДЕНО |

## Цепочки

### PHYSICAL

```text
Uref source -> [не найден компонент/net] -> [не найдена доступная точка]
```

- `Uref source -> ...`: НЕ ПОДТВЕРЖДЕНО.
- `... -> доступная измерительная точка`: НЕ ПОДТВЕРЖДЕНО.

### TELEMETRY

```text
[не установленный analog source]
-> MX_P1_CACSH[81] = 0x04010012
-> FIO1PIN
-> ADC result AD0DR0
-> [не найден adapter mapping]
-> [предположительно external reference204 addr98]
```

- source -> `MX_P1_CACSH[81]`: HYPOTHESIS.
- `MX_P1_CACSH[81] -> FIO1PIN -> AD0DR0`: CONFIRMED.
- `AD0DR0 -> adapter mapping -> external addr98`: НЕ ПОДТВЕРЖДЕНО.

## Итог

Пункт 1.1.4.9 не закрыт. До появления читаемой схемы/netlist ЯЛК и исходника
адаптера либо другого однозначного доказательства нельзя присваивать addr98
статус Uref и нельзя использовать addr99 как независимое доказательство:
addr99 уже участвует в калибровке 97/99. Production verdict
`yalk.reference` этим документом не изменяется.
