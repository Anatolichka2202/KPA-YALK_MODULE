# Контракт API ядра «Орбита-IV»

Заморожен 2026-06-15. Заголовок: [`orbita/include/orbita.h`](../orbita/include/orbita.h).

Ядро (`orbita::Orbita`) — чистый C++17, **не знает** про файлы, кодировки, SQLite,
Qt, графики и пороги. Всё это — задача UI-слоя. Граница общения — три POD-структуры.

## Что API принимает на вход

### `setChannels(const std::vector<ChannelSpec>& specs)`
Атомарная (горячая) замена набора каналов. Можно вызывать во время сбора.

```cpp
struct ChannelSpec {
    std::string address;   // НОРМАЛИЗОВАННЫЙ адрес, напр. "M16P1A70B12C10D10T01"
    std::string name;      // имя параметра (для оператора), может быть пустым
    std::string category;  // категория (для группировки), может быть пустой
};
```

**Требования к `address`:**
- Уже нормализован UI-слоем (`encoding::normalizeAddress`): только латиница,
  верхний регистр, без пробелов и комментариев.
- Кириллические двойники (М, П, Р, С, Т, Х …) и Latin-1-артефакты должны быть
  сведены к латинице **до** передачи в ядро.
- Формат: `M<инф><смещение>{A|B|C|D|E|X}<NN>…T<тип>`. Сейчас поддерживается только
  информативность `M16`; прочие будут отвергнуты парсером.
- Каналы, которые не распарсились, ядро пропускает с предупреждением в лог
  (не бросает исключение на весь набор).

### Источник данных
- `setDeviceE2010(int channel, double rate_khz)` — АЦП E20-10. **Бросает
  `orbita_error`**, если железа/DLL нет — UI обязан это перехватить.
- `setDeviceNone()` — режим без устройства (безопасный дефолт, приложение стартует).

## Что API отдаёт наружу

### `getSnapshot() → Snapshot`
Согласованный срез всех значений за один цикл.

```cpp
struct ChannelValue {
    std::string address;   // тот же ключ, что во входном ChannelSpec
    double      value;     // СЫРОЙ КОД канала (не вольты!)
    bool        valid;     // обновлялось ли значение в последнем цикле
};
struct Stats {
    int      phrase_error_percent;
    int      group_error_percent;
    uint64_t frames_processed;
    double   mb_per_second;
};
struct Snapshot {
    uint32_t                  mtv_seconds;  // бортовое время
    std::vector<ChannelValue> values;       // порядок = порядок setChannels
    Stats                     stats;
};
```

**`value` — это сырой код, а не физическая величина.** Перевод в вольты/градусы/
давление (масштаб, смещение, единицы) — задача UI-слоя. Тип канала задаёт способ
извлечения кода из слова:

| Тип адреса | T-код | Что в `value` |
|---|---|---|
| `ANALOG_10BIT` | T01 | 10-битный код 0..1023 |
| `ANALOG_9BIT` | T01-01 | 9-битный код 0..511 |
| `CONTACT` | T05 | бит 0 или 1 (по `bitNumber`) |
| `FAST_1` | T21 | 8-битный код 0..255 |
| `FAST_2` | T22 | 6-битный код (из двух слов) |
| `FAST_4` | T24 | 6-битный код (из двух слов) |
| `TEMPERATURE` | T11 | 8-битный код 0..255 |
| `BUS` | T25 | 16-битное значение (из двух слов) |
| `FAST_3` | T23 | сырое 12-битное слово (формат TODO) |

### Остальное чтение
- `getValueByAddress(addr) → std::optional<double>` — одно значение по ключу
  (nullopt, если канала нет или значение ещё не валидно).
- `getChannels() → std::vector<ChannelSpec>` — текущий активный набор (порядок
  совпадает с `Snapshot::values`).
- `getStats()`, `getCurrentTimeSeconds()`.

## Модели получения данных
1. **Pull**: `waitForData(timeout)` блокирует до новых данных, затем `getSnapshot()`.
   Используется в desktop-UI (таймер 100 мс).
2. **Push**: `setDataCallback(cb)` — `cb(const Snapshot&)` вызывается из декодерного
   потока на каждый цикл. Для серверного режима и RuleEngine. **Колбэк не должен
   блокировать** — он на горячем пути декодера.

## Жизненный цикл
```
setDeviceE2010 | setDeviceNone
setChannels(...)            // можно повторять на лету
start()
  pause() / resume()        // заморозка значений без остановки сбора
  startRecording / stopRecording
stop()
```
`isRunning()`, `isPaused()`, `isRecording()` — опрос состояния.

## Потоковая модель
- **Поток устройства**: E2010Device → callback → очередь отсчётов.
- **Декодерный поток**: очередь → BitstreamRecoverer → FrameDecoderM16 →
  `onDecoderGroup` пишет значения под `values_mutex_`.
- **Поток UI**: `getSnapshot()`/`getValueByAddress()` читают под тем же мьютексом.
- Конфигурация каналов — RCU через `ChannelRegistry` (читатели берут `shared_ptr`
  на неизменяемый snapshot без блокировок на время итерации).
