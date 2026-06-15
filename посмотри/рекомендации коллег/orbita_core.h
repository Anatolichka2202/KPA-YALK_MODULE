/**
 * @file orbita_core.h
 * @brief Внутренний контекст библиотеки Orbita.
 *
 * ИЗМЕНЕНИЯ vs оригинала:
 *   • channels_ (vector<ChannelDesc>) → ChannelRegistry (потокобезопасный)
 *   • Нет readerLoop() — E2010Device теперь сам управляет своим потоком
 *   • start() НЕ бросает исключение если каналы не заданы (пустой сбор)
 *   • onDecoderGroup() использует entry.poolIndex — баг с индексацией исправлен
 *   • Нет зависимости от Qt
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
#include <queue>

#include "channel_registry.h"
#include "device/e2010_device.h"
#include "decoder/frame_decoder_m16.h"
#include "decoder/fifo_buffer.h"
#include "decoder/bitstream_recoverer.h"

namespace orbita {

class AddressManager;
class DataPool;
class TlmWriter;

class Context {
public:
    Context();
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;

    // ─── Устройство ───────────────────────────────────────────────────────
    void setDeviceE2010(int channel, double sampleRateKHz);

    // ─── Каналы (можно менять на лету, пока декодер работает) ────────────
    void setChannelsFromFile (const std::string& filename);
    void setChannelsFromLines(const std::vector<std::string>& lines);
    void setChannelsFromPairs(
        const std::vector<std::pair<std::string,std::string>>& addressNamePairs);

    // ─── Управление сбором ────────────────────────────────────────────────
    void start();
    void stop();
    void setInvertSignal(bool invert);

    // ─── Подача сырых данных (тест / воспроизведение файла) ───────────────
    void feedRawData(const int16_t* samples, size_t count);

    // ─── TLM-запись ───────────────────────────────────────────────────────
    void startRecording(const std::string& filename);
    void stopRecording();

    // ─── Ожидание данных ──────────────────────────────────────────────────
    bool waitForData(std::chrono::milliseconds timeout);

    // ─── Чтение значений ─────────────────────────────────────────────────
    double getAnalog     (int idx) const;
    int    getContact    (int idx) const;
    int    getFast       (int idx) const;
    int    getTemperature(int idx) const;
    int    getBus        (int idx) const;

    int  getAnalogCount () const;
    int  getContactCount() const;

    std::string getChannelName    (int analogIdx) const;
    std::string getChannelCategory(int analogIdx) const;

    // ─── Статистика декодирования ─────────────────────────────────────────
    int getPhraseErrorPercent() const;
    int getGroupErrorPercent () const;

    // ─── Бортовое время ──────────────────────────────────────────────────
    uint32_t getCurrentTimeSeconds() const;

    // ─── Lua-заглушки ────────────────────────────────────────────────────
    void        loadScript        (const std::string& path);
    std::string runScriptFunction (const std::string& func, const std::string& args);

private:
    // ─── Компоненты ───────────────────────────────────────────────────────
    std::unique_ptr<E2010Device>     device_;
    std::unique_ptr<FrameDecoderM16> decoder_;
    std::unique_ptr<DataPool>        data_pool_;
    std::unique_ptr<TlmWriter>       tlm_writer_;

    ChannelRegistry registry_;   ///< горячая замена каналов + правильные poolIndex

    FifoBuffer         bitFifo_;
    BitstreamRecoverer recoverer_;

    // ─── Параметры ────────────────────────────────────────────────────────
    int  informativnost_  = 16;
    bool invert_signal_   = false;
    bool is_running_      = false;

    // ─── Поток декодера ───────────────────────────────────────────────────
    std::thread       decoder_thread_;
    std::atomic<bool> stop_worker_{false};

    // ─── Очередь сэмплов: device callback → decoder thread ───────────────
    std::queue<std::vector<int16_t>> sample_queue_;
    std::mutex                       queue_mutex_;
    std::condition_variable          queue_cv_;

    // ─── Синхронизация бортового времени ─────────────────────────────────
    mutable std::mutex      data_mutex_;
    std::condition_variable data_cv_;
    std::atomic<bool>       data_updated_{false};
    uint32_t                current_mtv_ = 0;

    // ─── Внутренние методы ────────────────────────────────────────────────
    void decoderLoop();
    void onDecoderGroup(const std::vector<uint16_t>& group12,
                        int groupNum, int ciklNum);

    /// Вспомогательный: загружает финализированные каналы в registry_ + resize DataPool
    void applyChannels(std::vector<ChannelDesc> descs,
                       const std::vector<std::string>& names = {},
                       const std::vector<std::string>& cats  = {});
};

} // namespace orbita
