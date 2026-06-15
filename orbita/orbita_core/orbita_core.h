#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>
#include <vector>
#include <queue>
#include <optional>

#include "../include/orbita.h"           // ChannelSpec, Snapshot, Stats, DataCallback
#include "../device/e2010_device.h"
#include "../decoder/frame_decoder_m16.h"
#include "../decoder/bitstream_recover.h"
#include "../decoder/fifo_buffer.h"
#include "channel_registry.h"

namespace orbita {

class Context {
public:
    Context();
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;

    // Источник данных
    void setDeviceE2010(int channel, double rate_khz);
    void setDeviceNone();

    // Каналы (горячая замена)
    void setChannels(const std::vector<ChannelSpec>& specs);
    std::vector<ChannelSpec> getChannels() const;

    // Жизненный цикл
    void start();
    void stop();
    void pause();
    void resume();
    bool isRunning() const { return is_running_.load(); }
    bool isPaused()  const { return paused_.load(); }
    void setInvertSignal(bool invert);

    // Запись TLM
    void startRecording(const std::string& filename);
    void stopRecording();
    bool isRecording() const;

    // Данные
    bool waitForData(std::chrono::milliseconds timeout);
    Snapshot getSnapshot() const;
    std::optional<double> getValueByAddress(const std::string& address) const;
    Stats getStats() const;
    uint32_t getCurrentTimeSeconds() const;
    void setDataCallback(DataCallback cb);

private:
    // Компоненты
    std::unique_ptr<E2010Device>      device_;
    std::unique_ptr<FrameDecoderM16>  decoder_;

    // Битовый конвейер
    FifoBuffer          bitFifo_;
    BitstreamRecoverer  recoverer_;

    // Реестр каналов (RCU snapshot)
    ChannelRegistry     registry_;

    // Живые значения, выровнены по entries текущего снапшота реестра
    mutable std::mutex   values_mutex_;
    std::vector<double>  values_;
    std::vector<uint8_t> valid_;

    // Параметры
    bool invert_signal_  = false;
    int  informativnost_ = 16;
    std::atomic<bool> is_running_{false};
    std::atomic<bool> paused_{false};

    // Декодерный поток
    std::thread       decoder_thread_;
    std::atomic<bool> stop_worker_{false};

    // Очередь отсчётов: Device → декодерный поток
    std::queue<std::vector<int16_t>> sample_queue_;
    std::mutex               queue_mutex_;
    std::condition_variable  queue_cv_;

    // Сигнализация «данные готовы»
    mutable std::mutex       data_mutex_;
    std::condition_variable  data_cv_;
    std::atomic<bool>        data_updated_{false};

    // Статистика
    std::atomic<uint64_t> samples_processed_{0};
    std::chrono::steady_clock::time_point start_time_;

    // Push-колбэк
    DataCallback data_callback_;
    std::mutex   callback_mutex_;

    // Приватные методы
    void ensureDecoder();
    void pushToQueue(const std::vector<int16_t>& samples);
    void decoderLoop();
    void onDecoderGroup(const std::vector<uint16_t>& group12, int groupNum, int ciklNum);

    // Сборка значений (вызывать под values_mutex_; cfg по размеру совпадает с values_)
    void fillValuesLocked(const ChannelRegistry::Snapshot& cfg, Snapshot& out) const;
};

} // namespace orbita
