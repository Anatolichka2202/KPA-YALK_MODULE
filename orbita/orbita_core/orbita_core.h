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

#include "../device/e2010_device.h"
#include "../decoder/frame_decoder_m16.h"
#include "../decoder/bitstream_recover.h"
#include "../decoder/fifo_buffer.h"
#include "../address/address_parser.h"

namespace orbita {

class AddressManager;
class DataPool;

class Context {
public:
    Context();
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;

    // Устройство
    void setDeviceE2010(int channel, double sample_rate_khz);

    // Загрузка адресов (все три варианта — можно вызывать на лету)
    void setChannelsFromFile(const std::string& filename);
    void setChannelsFromLines(const std::vector<std::string>& lines);
    void setChannelsFromPairs(const std::vector<std::pair<std::string, std::string>>& addressNamePairs);

    // Управление сбором
    void start();
    void stop();
    void setInvertSignal(bool invert);

    // Инъекция сырых данных (для воспроизведения из файла)
    void feedRawData(const int16_t* samples, size_t count);

    // TLM-запись
    void startRecording(const std::string& filename);
    void stopRecording();

    // Ожидание новых данных
    bool waitForData(std::chrono::milliseconds timeout);

    // Доступ к значениям
    double getAnalog(int idx) const;
    int    getContact(int idx) const;
    int    getFast(int idx) const;
    int    getTemperature(int idx) const;
    int    getBus(int idx) const;

    int  getAnalogCount() const;
    int  getContactCount() const;
    std::string getAnalogChannelName(int idx) const;

    // Статистика
    int getPhraseErrorPercent() const;
    int getGroupErrorPercent() const;

    // Заглушки Lua
    void loadScript(const std::string& script_path);
    std::string runScriptFunction(const std::string& func_name, const std::string& args_json);

    uint32_t getCurrentTimeSeconds() const;

private:
    // Компоненты
    std::unique_ptr<E2010Device>      device_;
    std::unique_ptr<FrameDecoderM16>  decoder_;
    std::unique_ptr<DataPool>         data_pool_;

    // Битовый конвейер
    FifoBuffer          bitFifo_;
    BitstreamRecoverer  recoverer_;

    // Параметры
    bool invert_signal_  = false;
    int  informativnost_ = 16;
    bool is_running_     = false;

    // Каналы + мьютекс для горячей замены конфига
    mutable std::mutex       channels_mutex_;
    std::vector<ChannelDesc> channels_;

    // Декодерный поток
    std::thread          decoder_thread_;
    std::atomic<bool>    stop_worker_{false};

    // Очередь отсчётов: Device → деккодерный поток
    std::queue<std::vector<int16_t>> sample_queue_;
    std::mutex               queue_mutex_;
    std::condition_variable  queue_cv_;

    // Синхронизация для пользователя (МТВ + данные готовы)
    mutable std::mutex       data_mutex_;
    std::condition_variable  data_cv_;
    std::atomic<bool>        data_updated_{false};
    uint32_t                 current_mtv_ = 0;

    // Приватные методы
    void ensureDecoder();
    void applyChannels(std::vector<ChannelDesc> descs);  // атомарная замена конфига
    void pushToQueue(const std::vector<int16_t>& samples);

    void decoderLoop();
    void onDecoderGroup(const std::vector<uint16_t>& group12,
                        int groupNum, int ciklNum);
};

} // namespace orbita
