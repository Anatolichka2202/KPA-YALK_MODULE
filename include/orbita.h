/**
 * @file orbita.h
 * @brief Главный API библиотеки сбора телеметрии "Орбита-IV" (C++ версия).
 *
 * Использование:
 *   orbita::Orbita orb;
 *   orb.setDeviceE2010(0, 10000.0);
 *   orb.setChannelsFromFile("addresses.txt");
 *   orb.start();
 *   while (orb.waitForData(std::chrono::milliseconds(1000))) {
 *       double volt = orb.getAnalog(0);
 *       // ...
 *   }
 *   orb.stop();
 */

#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <stdexcept>

namespace orbita {

// Базовый класс для всех исключений библиотеки
class orbita_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// -----------------------------------------------------------------
// Основной класс контекста (скрытая реализация)
// -----------------------------------------------------------------
class Context;

// -----------------------------------------------------------------
// Главный API класс
// -----------------------------------------------------------------
class Orbita {
public:
    Orbita();
    ~Orbita();

    // Запрет копирования
    Orbita(const Orbita&) = delete;
    Orbita& operator=(const Orbita&) = delete;

    // Разрешение перемещения
    Orbita(Orbita&&) noexcept;
    Orbita& operator=(Orbita&&) noexcept;

    // -----------------------------------------------------------------
    // Настройка устройств
    // -----------------------------------------------------------------

    /// АЦП E20-10
    /// @param channel         номер канала 0..3
    /// @param sample_rate_khz частота дискретизации в кГц (обычно 10000)
    /// @throws orbita_error при ошибке инициализации
    void setDeviceE2010(int channel, double sample_rate_khz);

    // LimeSDR (будет позже)
    // void setDeviceLimeSDR(const std::string& params);

    // Вольтметр через VISA
    // void setVoltmeterVISA(const std::string& resource);

    // -----------------------------------------------------------------
    // Загрузка адресов (список каналов)
    // -----------------------------------------------------------------

    /// Загрузить адреса из текстового файла (формат "M16...Txx")
    /// @throws orbita_error при ошибках чтения/парсинга
    void setChannelsFromFile(const std::string& filename);

    /// Альтернатива: передать список строк напрямую
    void setChannelsFromLines(const std::vector<std::string>& lines);

    // -----------------------------------------------------------------
    // Управление сбором данных
    // -----------------------------------------------------------------

    /// Запуск непрерывного сбора и декодирования
    /// @throws orbita_error при ошибке запуска
    void start();

    /// Остановка сбора (ждёт завершения рабочего потока)
    void stop();

    /// Инвертировать битовый сигнал (если полярность инверсная)
    void setInvertSignal(bool invert);

    // -----------------------------------------------------------------
    // Запись телеметрии в TLM-файл
    // -----------------------------------------------------------------

    /// Начать запись в файл (формат .tlm). Запись идёт в фоне.
    void startRecording(const std::string& filename);

    /// Остановить запись и закрыть файл
    void stopRecording();

    // -----------------------------------------------------------------
    // Получение данных (блокирующее ожидание)
    // -----------------------------------------------------------------

    /// Ожидать поступления новой группы кадров (все 32 группы цикла)
    /// @param timeout максимальное время ожидания
    /// @return true – данные готовы, false – таймаут
    bool waitForData(std::chrono::milliseconds timeout);

    // -----------------------------------------------------------------
    // Доступ к текущим значениям каналов
    // (индексы соответствуют порядку добавления каналов в setChannels*)
    // -----------------------------------------------------------------

    double getAnalog(int idx) const;
    int    getContact(int idx) const;
    int    getFast(int idx) const;
    int    getTemperature(int idx) const;
    int    getBus(int idx) const;

    // -----------------------------------------------------------------
    // Статистика декодирования (проценты ошибок маркеров)
    // -----------------------------------------------------------------

    int getPhraseErrorPercent() const;
    int getGroupErrorPercent() const;

    // -----------------------------------------------------------------
    // Lua-скриптинг (опционально)
    // -----------------------------------------------------------------

    void loadScript(const std::string& script_path);
    std::string runScriptFunction(const std::string& func_name, const std::string& args_json);

private:
    std::unique_ptr<Context> ctx_;
};

} // namespace orbita
