/**
 * @file orbita.h
 * @brief Публичный API декодера телеметрии «Орбита-IV» (C++17).
 *
 * Архитектурная граница:
 *   • liborbita декодирует поток отсчётов и НЕ выбирает физическое
 *     оборудование станции;
 *   • вход сырых данных — pushSamples(); источник выбирает MilTechStation
 *     по профилю конкретной поставки;
 *   • liborbita не зависит от API конкретного АЦП или драйвера станции.
 *
 * Контракт ядра:
 *   • ядро НЕ знает про файлы конфигурации поставки, SQLite, Qt, UI, допуски;
 *   • вход — raw int16 samples + std::vector<ChannelSpec>;
 *   • выход — Snapshot со значениями ПО АДРЕСУ (а не по индексу).
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

    // ----- Входной поток -----
    /// Передать очередную порцию сырых отсчётов. Физический источник создаётся
    /// и обслуживается уровнем станции/интеграции, а не liborbita.
    void pushSamples(const std::vector<int16_t>& samples);

    // ----- Каналы (горячая замена на лету) -----
    /// Атомарно заменяет набор каналов. Можно вызывать во время сбора.
    void setChannels(const std::vector<ChannelSpec>& specs);

    /// Текущий активный набор каналов.
    std::vector<ChannelSpec> getChannels() const;

    // ----- Жизненный цикл декодера -----
    void start();
    void stop();
    void pause();           ///< заморозить значения без остановки декодера
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
