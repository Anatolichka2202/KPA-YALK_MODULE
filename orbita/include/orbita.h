/**
 * @file orbita.h
 * @brief Публичный API библиотеки сбора телеметрии «Орбита-IV» (C++17).
 *
 * Контракт (заморожен 2026-06-15):
 *   • Ядро НЕ знает про файлы, кодировки, SQLite, Qt, графики, пороги.
 *   • Вход — std::vector<ChannelSpec> (адрес уже нормализован UI-слоем).
 *   • Выход — Snapshot со значениями ПО АДРЕСУ (а не по индексу).
 *
 * Пример:
 *   orbita::Orbita orb;
 *   orb.setDeviceE2010(0, 10000.0);
 *   orb.setChannels({ {"M16P1A70B12C10D10T01", "Давление БС", "Давления"} });
 *   orb.start();
 *   while (orb.waitForData(std::chrono::milliseconds(1000))) {
 *       auto snap = orb.getSnapshot();
 *       for (const auto& v : snap.values)
 *           if (v.valid) use(v.address, v.value);
 *   }
 *   orb.stop();
 */

#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <optional>
#include <functional>

namespace orbita {

/// Базовый класс для всех исключений библиотеки.
class orbita_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// -----------------------------------------------------------------
//  POD-структуры контракта
// -----------------------------------------------------------------

/// Один канал на входе. Адрес уже нормализован UI-слоем.
struct ChannelSpec {
    std::string address;    ///< нормализованный адрес, напр. "M16P1A70B12C10D10T01"
    std::string name;       ///< имя параметра (для оператора), может быть пустым
    std::string category;   ///< категория (для группировки), может быть пустой
};

/// Значение одного канала на выходе.
struct ChannelValue {
    std::string address;    ///< тот же ключ, что во входном ChannelSpec
    double      value = 0.0;///< декодированное значение (сырой код канала)
    bool        valid = false; ///< было ли значение обновлено в последнем цикле
};

/// Статистика декодирования.
struct Stats {
    int      phrase_error_percent = 0;
    int      group_error_percent  = 0;
    uint64_t frames_processed     = 0;   ///< счётчик обработанных групп
    double   mb_per_second        = 0.0; ///< пропускная способность входного потока
};

/// Согласованный срез состояния (значения из одного цикла + МТВ + статистика).
struct Snapshot {
    uint32_t                  mtv_seconds = 0;
    std::vector<ChannelValue> values;
    Stats                     stats;
};

/// Колбэк новых данных. Вызывается из декодерного потока (push-модель).
using DataCallback = std::function<void(const Snapshot&)>;

// -----------------------------------------------------------------
//  Скрытая реализация
// -----------------------------------------------------------------
class Context;

// -----------------------------------------------------------------
//  Главный API класс
// -----------------------------------------------------------------
class Orbita {
public:
    Orbita();
    ~Orbita();

    Orbita(const Orbita&) = delete;
    Orbita& operator=(const Orbita&) = delete;
    Orbita(Orbita&&) noexcept;
    Orbita& operator=(Orbita&&) noexcept;

    // ----- Источник данных -----
    /// АЦП E20-10. @param channel 0..3, @param rate_khz частота кГц (обычно 10000).
    /// @throws orbita_error при ошибке инициализации (окно UI должно это пережить).
    void setDeviceE2010(int channel, double rate_khz);

    /// Без устройства (значения только через декодирование внешнего потока).
    /// Безопасный режим по умолчанию — приложение стартует без оборудования.
    void setDeviceNone();

    // ----- Каналы (горячая замена на лету) -----
    /// Атомарно заменяет набор каналов. Можно вызывать во время сбора.
    void setChannels(const std::vector<ChannelSpec>& specs);

    /// Текущий активный набор каналов.
    std::vector<ChannelSpec> getChannels() const;

    // ----- Жизненный цикл -----
    void start();
    void stop();
    void pause();           ///< заморозить значения без остановки сбора
    void resume();
    bool isRunning() const;
    bool isPaused() const;

    /// Инвертировать битовый сигнал (если полярность инверсная).
    void setInvertSignal(bool invert);

    // ----- Запись телеметрии -----
    void startRecording(const std::string& filename);
    void stopRecording();
    bool isRecording() const;

    // ----- Получение данных -----
    /// Ожидать поступления новых данных. @return true — данные готовы, false — таймаут.
    bool waitForData(std::chrono::milliseconds timeout);

    /// Согласованный срез всех значений за один цикл.
    Snapshot getSnapshot() const;

    /// Значение конкретного канала по адресу (nullopt — нет такого канала).
    std::optional<double> getValueByAddress(const std::string& address) const;

    /// Статистика декодирования.
    Stats getStats() const;

    /// Бортовое время в секундах.
    uint32_t getCurrentTimeSeconds() const;

    /// Push-уведомление о новых данных (для серверного режима / RuleEngine).
    void setDataCallback(DataCallback cb);

private:
    std::unique_ptr<Context> ctx_;
};

} // namespace orbita
