# Equipment Plugin ABI

Код контракта:

```text
stand/include/orbita_stand/equipment_plugin.h
```

Runtime загрузки:

```text
stand/src/equipment_runtime.cpp
```

---

# Зачем нужен ABI

Equipment Plugin ABI позволяет MilTechStation загружать отдельно
собранные плагины оборудования:

```text
MilTechStation
      ↓
C ABI v1
      ↓
DLL / SO / DYLIB
      ↓
конкретное оборудование
```

Плагин может быть собран отдельно от основного приложения.

Поэтому через ABI не передаются Qt-классы, STL-контейнеры и C++ объекты.

Граница построена на C-совместимых структурах, указателях и числовых
типах.

---

# Версия

Текущая версия:

```c
ORBITA_EQUIPMENT_ABI_V1 = 0x00010000
```

---

# Export symbol

Каждый Equipment Plugin обязан экспортировать:

```c
orbita_plugin_get_api_v1
```

Сигнатура:

```c
const orbita_equipment_api_v1*
orbita_plugin_get_api_v1(void);
```

Runtime ищет именно этот symbol.

DLL без него не считается Equipment Plugin.

---

# Таблица API плагина

Плагин возвращает:

```c
struct orbita_equipment_api_v1 {
    uint32_t abi_version;
    uint32_t struct_size;

    const char* plugin_id;
    const char* display_name_utf8;
    const char* capabilities_utf8;

    create(...);
    destroy(...);
    invoke(...);
    cancel(...);
    safe_stop(...);
};
```

## Metadata

`plugin_id`

Стабильный машинный идентификатор плагина.

В одном каталоге нельзя загрузить два плагина с одинаковым `plugin_id`.

`display_name_utf8`

Человеческое имя.

`capabilities_utf8`

Список capability-id через `;`.

Например:

```text
measure.reference_voltage;measure.dc_current
```

---

# Проверка совместимости при загрузке

Runtime проверяет:

```text
api != null
abi_version == ORBITA_EQUIPMENT_ABI_V1
struct_size >= sizeof(orbita_equipment_api_v1)

plugin_id != null

create != null
destroy != null
invoke != null
cancel != null
safe_stop != null
```

Несовместимый плагин не используется.

---

# Жизненный цикл экземпляра

## create

```text
DLL загружена
    ↓
create(instance_id, config)
    ↓
void* instance
```

`instance_id` и конфигурация передаются как UTF-8 текст.

## invoke

Основной вызов:

```text
capability
operation
request
    ↓
plugin
    ↓
response
```

## cancel

Просьба отменить текущую операцию.

## safe_stop

Просьба привести оборудование в безопасное состояние.

Для активного оборудования `safe_stop` является обязательной частью
контракта.

## destroy

Освобождает экземпляр.

---

# Статусы

ABI v1 определяет:

```text
ORBITA_PLUGIN_OK
ORBITA_PLUGIN_INVALID_ARGUMENT
ORBITA_PLUGIN_NOT_SUPPORTED
ORBITA_PLUGIN_NOT_READY
ORBITA_PLUGIN_IO_ERROR
ORBITA_PLUGIN_BUFFER_TOO_SMALL
ORBITA_PLUGIN_CANCELLED
ORBITA_PLUGIN_INTERNAL_ERROR
```

Статус ошибки оборудования не является verdict изделия.

---

# Буфер ответа

Через границу используется:

```c
struct orbita_plugin_buffer_v1 {
    char* data;
    size_t capacity;
    size_t size;
};
```

Плагин записывает фактический необходимый размер в `size`.

Если `capacity` недостаточна:

```text
ORBITA_PLUGIN_BUFFER_TOO_SMALL
```

Runtime может повторить вызов с большим буфером.

---

# Формат config / request / response

Для текущего ABI используется простой UTF-8 формат:

```text
key=value
key=value
key=value
```

Одна пара на строку.

Пример:

```text
host=192.168.0.101
timeout_ms=2000
```

Спецсимволы экранируются обратным слешем.

Формат предназначен для простой ABI-границы.

Он не является внешним сетевым протоколом.

---

# Что ABI НЕ определяет

ABI не определяет:

- какие приборы должны быть в КТМА;
- какие capability обязательны для УБСИ;
- конкретные SCPI-команды;
- конкретные ROKT-команды;
- требования ТУ;
- операторский интерфейс.

Это отдельные уровни документации.
