# Документация MilTechStation / КТМА

> Это единственная точка входа в каноническую документацию.

## Контекст проекта

```text
Репозиторий
    │
    ├── MilTechStation        продукт / платформа
    │
    └── КТМА                  конкретная поставка
         │
         └── УБСИ             текущий объект работ
```

Подробно: [Границы проекта](00_BOUNDARIES.md).

---

# Читать по задаче

| Нужно понять | Документ |
|---|---|
| Где проходит граница репозитория, продукта, поставки и задачи | [00_BOUNDARIES.md](00_BOUNDARIES.md) |
| Что такое MilTechStation как продукт | [product/miltechstation.md](product/miltechstation.md) |
| Что такое поставка КТМА | [delivery/ktma.md](delivery/ktma.md) |
| Что сейчас делаем с УБСИ | [task/ubsi.md](task/ubsi.md) |

Остальные разделы будут мигрированы поэтапно.

---

## Исследования и доказательства

| Нужно понять | Документ |
|---|---|
| Как устроен research-контур | [research/INDEX.md](research/INDEX.md) |
| Какие программы являются референсными | [research/reference-programs.md](research/reference-programs.md) |
| Что действительно подтверждено на живом стенде | [research/stand.md](research/stand.md) |

## Технический reference

| Нужно понять | Документ |
|---|---|
| Карта API / ABI / протоколов | [reference/INDEX.md](reference/INDEX.md) |
| API liborbita и Equipment Registry | [reference/api.md](reference/api.md) |
| ABI DLL-плагинов оборудования | [reference/abi.md](reference/abi.md) |
| Протокол «Орбита» | [reference/protocols/orbita.md](reference/protocols/orbita.md) |
| ROKT | [reference/protocols/rokt.md](reference/protocols/rokt.md) |
| VISA / SCPI | [reference/protocols/scpi-visa.md](reference/protocols/scpi-visa.md) |
| HTTP ИСД | [reference/protocols/isd-http.md](reference/protocols/isd-http.md) |
| liborbita как компонент | [reference/components/liborbita.md](reference/components/liborbita.md) |
| Плагины оборудования | [reference/components/equipment-plugins.md](reference/components/equipment-plugins.md) |

## Данные, отчётность и администрирование

| Нужно понять | Документ |
|---|---|
| Общая модель данных MilTechStation | [product/data.md](product/data.md) |
| Где что хранит поставка КТМА | [delivery/data.md](delivery/data.md) |
| Какие документы формирует КТМА | [delivery/reporting.md](delivery/reporting.md) |
| Как учитываются изделие, состав, замены и этапы | [delivery/administration.md](delivery/administration.md) |
| Как хранится production run УБСИ | [task/ubsi/production-data.md](task/ubsi/production-data.md) |

## УБСИ — текущий объект

| Нужно понять | Документ |
|---|---|
| Что такое текущий объект УБСИ | [task/ubsi/overview.md](task/ubsi/overview.md) |
| Что требует нормативный источник | [task/ubsi/tu.md](task/ubsi/tu.md) |
| Как требования закрываются стендом | [task/ubsi/tu-work.md](task/ubsi/tu-work.md) |
| Как физически выполняется испытание | [task/ubsi/testing.md](task/ubsi/testing.md) |
| Где лежат первичные источники | [sources/INDEX.md](sources/INDEX.md) |
| Что такое внешний локальный архив | [research/external-archive.md](research/external-archive.md) |

## Сборка и эксплуатация

| Нужно сделать | Читать |
|---|---|
| Собрать проект | [product/build.md](product/build.md) |
| Понять оборудование и сеть стенда | [delivery/stand.md](delivery/stand.md) |
| Перенести release на стенд | [delivery/deployment.md](delivery/deployment.md) |
| Работать с ЯВП | [task/ubsi/yvp.md](task/ubsi/yvp.md) |

# Будущие разделы канона

Они создаются только по мере переноса подтверждённых знаний.

```text
docs/
├── product/          возможности MilTechStation
├── delivery/         конкретные поставки
├── task/             текущий объект/задача
├── reference/        API, ABI, протоколы, компоненты
├── research/         исследования и доказательства
├── sources/          первичные документы
├── notes/            opt-in знания вне текущего объекта
└── assets/           изображения и схемы
```

---

# Статус старой документации

Репозиторий находится в процессе миграции документации.

Старые файлы пока физически могут оставаться в `docs/`, `_plans/`,
`design-prototype/` и других местах, чтобы из них можно было извлечь
уникальные подтверждённые знания.

**Они не являются новым каноном.**

Агент не должен читать их автоматически.

Старый документ используется только когда:

1. текущий этап миграции явно требует его разобрать;
2. необходимо проверить происхождение конкретного факта;
3. пользователь прямо указал на этот документ.

После переноса полезной информации старые документальные артефакты будут
удалены. История их редакций остаётся в Git.

---

# Текущий объект

**УБСИ**.

Другие изделия КТМА не входят в рабочий контекст по умолчанию.

Документацию других изделий открывать только по явной задаче.

---

# Obsidian

Каталог `docs/` является Obsidian vault.

Открывать в Obsidian следует именно директорию:

```text
docs/
```

Стартовая заметка:

```text
INDEX.md
```

Внутренние ссылки оформляются обычными относительными Markdown-ссылками,
поэтому документация остаётся читаемой и вне Obsidian.
