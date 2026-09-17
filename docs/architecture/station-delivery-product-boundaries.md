# MilTechStation: границы station / delivery / product

Статус: базовая архитектурная спецификация для production-интеграции.

## 1. Ось зависимостей

Зависимости идут только в одну сторону:

```text
station platform + station UI
            ↓
      delivery package
      + delivery UI
            ↓
       product module
       + product UI
```

Предметный UI может зависеть от UI-контракта своей поставки.
UI поставки может зависеть от контрактов станции.
Станция не должна зависеть от КТМА, УБСИ, ППБ или другого изделия.

КТМА — поставка. УБСИ — первый production product-module внутри КТМА.
ППБ может прийти со своим полноценным интерфейсом и не обязан выглядеть как УБСИ.

## 2. Владение UI

### Station UI

Владеет только платформенными функциями:

- shell приложения и общей навигацией;
- оболочкой администрирования;
- логами, диагностикой и просмотром run/evidence;
- общим состоянием оборудования/runtime;
- host-ом для просмотра/редактирования артефактов;
- реестрами document/scenario/execution providers.

Station UI не владеет workflow УБСИ, семантикой регистратора КТМА или страницами ППБ.

### Delivery UI

Владеет общими для конкретной поставки функциями и сервисами.
Для КТМА это место для полного состава КТМА: stand profile, регистратора,
сервисных страниц оборудования и регистрации предметных модулей.

Поставка определяет, какие product-modules входят в сборку и какие optional runtime
ей разрешены.

### Product UI

Владеет предметным операторским UX.
Для УБСИ это Production/TУ workspace и представления Питание/ЯЛК/ЯТП/ЯВП.
Они не превращаются в универсальный экран, автоматически рисуемый из YAML.

ППБ может сохранить собственный UI.
Компонент переносится в station UI только после появления второго реального
потребителя, который подтверждает, что компонент действительно общий.

## 3. Администрирование и форматы

Администрирование — функция станции, но форматы файлов являются providers,
а не частью архитектуры станции.

Артефакты разделяются по смыслу:

```text
document/config     -> codec/editor/validator
scenario            -> ScenarioProvider
executable script   -> ExecutionRuntime
binary/data         -> viewer/importer
```

Целевая модель providers:

- YAML — текущий ScenarioEngine/document provider;
- JSON — provider на Qt JSON;
- INI — provider на QSettings;
- TOML — optional provider; зависимость toml++ добавляется только с первым
  реальным TOML consumer;
- TXT — plain-text provider без выдуманной схемы;
- Lua — executable runtime/provider, по умолчанию запрещён;
- Python — сначала external-process integration, embedded Python не требуется
  для первой интеграции.

Для текстовых артефактов допустим общий raw-text editor.
Typed editor — дополнительный contribution поставки или изделия.
Станция не должна молча интерпретировать INI/TOML/JSON/TXT как YAML.

## 4. Граница сценариев

Формат сценария и lifecycle запуска — разные сущности:

```text
scenario artifact
      ↓
ScenarioProvider
      ↓
common run request / events / result / evidence
```

Существующий YAML ScenarioEngine является provider №1.
ППБ в будущем может подключить Lua ScenarioProvider и при этом публиковать тот же
run/evidence lifecycle.
Существующая программа на Python подключается через process runtime и не обязана
переписываться на C++.

Исполняемые runtime выключены по умолчанию.
Разрешение конкретного runtime задаёт delivery composition: например, ППБ может
разрешить Lua, а КТМА/УБСИ не обязаны его иметь.

## 5. Стек

Текущая production-база:

- C++17;
- Qt 6 / Qt Widgets;
- CMake;
- yaml-cpp для существующих YAML-сценариев/конфигов;
- SQLite в уже существующих registrar/report/run контурах;
- equipment plugins через station equipment ABI.

Целевые platform providers:

- Qt JSON — JSON;
- QSettings — INI;
- toml++ — TOML при появлении реального consumer;
- Lua 5.4 — optional delivery-enabled runtime;
- Python 3 — сначала внешний процесс;
- QFile/QTextStream — TXT.

Ветка `universal_miltechstation` используется как донор StationSession,
component/resource composition и process execution runtime.
Целиком в production-ветку она не вливается.

## 6. Целевая структура репозитория

```text
platform/
  ui/                       station-facing UI contracts

apps/desktop/               concrete station desktop shell

deliveries/ktma/
  ui/                       KTMA delivery UI contract/composition
  registrar/
  ubsi/
    backend/domain
    ui/                     UBSI product UI module

station/                    reusable runtime/equipment/scenario infrastructure
```

Первый реализованный срез вводит compile-time границу
`station -> KTMA delivery -> UBSI product` без изменения утверждённого
операторского интерфейса УБСИ.
