/**
 * @file orbita_core.h
 * @brief Внутренний контекст библиотеки Orbita (скрытая реализация).
 */

#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>
#include <vector>
#include "../device/e2010_device.h"
namespace orbita {

// Предварительные объявления
//class E2010Device;
class FrameDecoder;
class AddressManager;
class DataPool;
class TlmWriter;

/**
 * @brief Внутренний контекст, содержащий все компоненты системы.
 *        Создаётся и управляется классом Orbita.
 */
class Context {
public:
    Context();
    ~Context();

    // Запрет копирования
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // Разрешение перемещения (опционально)
    Context(Context&&) = delete;

    // -----------------------------------------------------------------
    // Настройка устройств
    void setDeviceE2010(int channel, double sample_rate_khz);

    // -----------------------------------------------------------------
    // Адреса каналов
    void setChannelsFromFile(const std::string& filename);
    void setChannelsFromLines(const std::vector<std::string>& lines);

    // -----------------------------------------------------------------
    // Управление сбором
    void start();
    void stop();
    void setInvertSignal(bool invert);

    // -----------------------------------------------------------------
    // TLM-запись
    void startRecording(const std::string& filename);
    void stopRecording();

    // -----------------------------------------------------------------
    // Получение данных
    bool waitForData(std::chrono::milliseconds timeout);
    double getAnalog(int idx) const;
    int getContact(int idx) const;
    int getFast(int idx) const;
    int getTemperature(int idx) const;
    int getBus(int idx) const;

    // -----------------------------------------------------------------
    // Статистика
    int getPhraseErrorPercent() const;
    int getGroupErrorPercent() const;

    // -----------------------------------------------------------------
    // Lua (заглушки)
    void loadScript(const std::string& script_path);
    std::string runScriptFunction(const std::string& func_name, const std::string& args_json);

private:
    // Компоненты
    std::unique_ptr<E2010Device> device_;
    std::unique_ptr<FrameDecoder> decoder_;
    std::unique_ptr<AddressManager> addr_mgr_;
    std::unique_ptr<DataPool> data_pool_;
    std::unique_ptr<TlmWriter> tlm_writer_;

    // Параметры
    bool invert_signal_ = false;
    int informativnost_ = 16;

    // Поток захвата данных
    std::thread worker_thread_;
    std::atomic<bool> stop_worker_{false};
    std::atomic<bool> is_running_{false};

    // Синхронизация для пользовательских данных
    mutable std::mutex data_mutex_;
    std::condition_variable data_cv_;
    std::atomic<bool> new_data_available_{false};

    // Внутренние методы
    void workerLoop();
    void onDecoderGroup(const std::vector<uint16_t>& group_words);
};

} // namespace orbita
