#include "orbita_core.h"
#include "orbita.h" // для исключения orbita_error
#include "../address/address_manager.h"
#include "../decoder/decoder_factory.h"
#include "data_pool.h"
#include "../tlm/tlm_writer.h"
#include "../value/value_converter.h"
#include "logger.h"
#include "../include/orbita_types.h"
#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>
#include <queue>
namespace orbita {
// -----------------------------------------------------------------
// Вспомогательные функции (внутренние)
// -----------------------------------------------------------------
static void computeThreshold(const std::vector<int16_t>& samples, int& threshold) {
    if (samples.size() < 100) {
        threshold = 0;
        return;
    }
    int minVal = samples[0], maxVal = samples[0];
    size_t n = samples.size() / 10;
    for (size_t i = 0; i < n; ++i) {
        if (samples[i] < minVal) minVal = samples[i];
        if (samples[i] > maxVal) maxVal = samples[i];
    }
    threshold = (minVal + maxVal) / 2;
    LOG_INFO("Threshold computed: %d", threshold);
}

// -----------------------------------------------------------------
// Конструктор / деструктор
// -----------------------------------------------------------------
Context::Context() {
    data_pool_ = std::make_unique<DataPool>();
    LOG_INFO("Orbita context created");
}

Context::~Context() {
    stop();
    LOG_INFO("Orbita context destroyed");
}

// -----------------------------------------------------------------
// Настройка устройства
// -----------------------------------------------------------------
void Context::setDeviceE2010(int channel, double sample_rate_khz) {
    LOG_INFO("setDeviceE2010: channel=%d, rate=%.1f", channel, sample_rate_khz);
    auto dev = std::make_unique<E2010Device>();
    // В E2010Device init принимает slot и sampleRateKHz, channel пока не используем
    if (!dev->init(0, sample_rate_khz)) {
        throw orbita_error("E2010 init failed");
    }
    // TODO: установить channel, если нужно – можно добавить метод setChannel позже
    device_ = std::move(dev);
    LOG_INFO("E2010Device init OK");
}
// -----------------------------------------------------------------
// Адреса каналов
// -----------------------------------------------------------------
void Context::setChannelsFromFile(const std::string& filename) {
    LOG_INFO("Loading addresses from: %s", filename.c_str());
    auto mgr = std::make_unique<AddressManager>(informativnost_);
    mgr->loadFromFile(filename);
    addr_mgr_ = std::move(mgr);

    // Создаём декодер на основе информативности
    decoder_ = createFrameDecoder(addr_mgr_->getInformativnost());
    if (!decoder_) {
        throw orbita_error("Failed to create frame decoder");
    }
    decoder_->setCallback([this](const std::vector<uint16_t>& group) {
        onDecoderGroup(group);
    });

    // Настраиваем DataPool под количество каналов каждого типа
    const auto& channels = addr_mgr_->getChannels();
    int analog_cnt = 0, contact_cnt = 0, fast_cnt = 0, temp_cnt = 0, bus_cnt = 0;
    for (const auto& ch : channels) {
        switch (ch.adressType) {
        case ORBITA_ADDR_TYPE_ANALOG_10BIT:
        case ORBITA_ADDR_TYPE_ANALOG_9BIT: ++analog_cnt; break;
        case ORBITA_ADDR_TYPE_CONTACT: ++contact_cnt; break;
        case ORBITA_ADDR_TYPE_FAST_1:
        case ORBITA_ADDR_TYPE_FAST_2:
        case ORBITA_ADDR_TYPE_FAST_3:
        case ORBITA_ADDR_TYPE_FAST_4: ++fast_cnt; break;
        case ORBITA_ADDR_TYPE_TEMPERATURE: ++temp_cnt; break;
        case ORBITA_ADDR_TYPE_BUS: ++bus_cnt; break;
        default: break;
        }
    }
    data_pool_->resize(analog_cnt, contact_cnt, fast_cnt, temp_cnt, bus_cnt);
    LOG_INFO("Addresses loaded: %zu channels", channels.size());
}

void Context::setChannelsFromLines(const std::vector<std::string>& lines) {
    // Аналогично, но загружаем из строк
    auto mgr = std::make_unique<AddressManager>(informativnost_);
    mgr->loadFromLines(lines);
    addr_mgr_ = std::move(mgr);
    decoder_ = createFrameDecoder(addr_mgr_->getInformativnost());
    decoder_->setCallback([this](const std::vector<uint16_t>& group) {
        onDecoderGroup(group);
    });
    const auto& channels = addr_mgr_->getChannels();
    // ... (аналогичная настройка data_pool)
    LOG_INFO("Addresses loaded from lines: %zu channels", channels.size());
}

// -----------------------------------------------------------------
// Управление сбором
// -----------------------------------------------------------------
void Context::start() {
    if (is_running_) {
        throw orbita_error("Already running");
    }
    if (!device_) {
        throw orbita_error("No device configured");
    }
    if (!decoder_) {
        throw orbita_error("No channels configured (call setChannelsFromFile first)");
    }

    stop_worker_ = false;
    worker_thread_ = std::thread(&Context::workerLoop, this);
    is_running_ = true;
    LOG_INFO("Worker thread started");
}

void Context::stop() {
    if (!is_running_) return;
    stop_worker_ = true;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    is_running_ = false;
    LOG_INFO("Worker thread stopped");
}

void Context::setInvertSignal(bool invert) {
    invert_signal_ = invert;
}

// -----------------------------------------------------------------
// TLM-запись
// -----------------------------------------------------------------
void Context::startRecording(const std::string& filename) {
    if (tlm_writer_) {
        stopRecording();
    }
    tlm_writer_ = std::make_unique<TlmWriter>(filename, informativnost_);
    LOG_INFO("Recording started: %s", filename.c_str());
}

void Context::stopRecording() {
    tlm_writer_.reset();
    LOG_INFO("Recording stopped");
}

// -----------------------------------------------------------------
// Рабочий цикл (поток)
// -----------------------------------------------------------------
void Context::workerLoop() {
    // Очередь для сэмплов
    std::queue<std::vector<int16_t>> sampleQueue;
    std::mutex queueMutex;
    std::condition_variable queueCv;

    // Подписываемся на сигнал
    QObject::connect(device_.get(), &E2010Device::samplesReady,
                     [&](const std::vector<int16_t>& samples) {
                         std::lock_guard<std::mutex> lock(queueMutex);
                         sampleQueue.push(samples);
                         queueCv.notify_one();
                     });

    if (!device_->start()) {
        LOG_ERROR("Worker thread: failed to start device stream");
        return;
    }

    bool threshold_computed = false;
    int threshold = 0;
    size_t total_samples = 0;

    std::ofstream raw_file("raw_samples.bin", std::ios::binary);

    while (!stop_worker_) {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (!queueCv.wait_for(lock, std::chrono::milliseconds(100),
                              [&]{ return !sampleQueue.empty() || stop_worker_; })) {
            continue;
        }
        if (stop_worker_) break;

        auto samples = std::move(sampleQueue.front());
        sampleQueue.pop();
        lock.unlock();

        total_samples += samples.size();

        if (raw_file.is_open()) {
            raw_file.write(reinterpret_cast<const char*>(samples.data()), samples.size() * sizeof(int16_t));
        }

        if (!threshold_computed && samples.size() > 100) {
            computeThreshold(samples, threshold);
            threshold_computed = true;
            LOG_INFO("Threshold computed: %d", threshold);
        }

        // Преобразуем в биты
        std::vector<uint8_t> bits;
        bits.reserve(samples.size());
        for (int16_t v : samples) {
            uint8_t bit = (v >= threshold) ? 1 : 0;
            if (invert_signal_) bit = !bit;
            bits.push_back(bit);
        }
        decoder_->feedBits(bits.data(), bits.size());
    }

    raw_file.close();
    device_->stop();
    LOG_INFO("Worker thread finished. Total samples processed: %zu", total_samples);
}
// -----------------------------------------------------------------
// Обработка готовой группы (вызывается из callback декодера)
// -----------------------------------------------------------------
void Context::onDecoderGroup(const std::vector<uint16_t>& group_words) {
    LOG_INFO("onDecoderGroup called with %zu words", group_words.size());
    if (!addr_mgr_ || !data_pool_) return;

    const auto& channels = addr_mgr_->getChannels();
    size_t word_count = group_words.size();

    int analog_idx = 0, contact_idx = 0, fast_idx = 0, temp_idx = 0, bus_idx = 0;

    for (const auto& ch : channels) {
        if (ch.numOutElemG > word_count) {
            LOG_WARNING("Channel: word index %u out of range", ch.numOutElemG);
            continue;
        }
        uint16_t word = group_words[ch.numOutElemG - 1];

        switch (ch.adressType) {
        case ORBITA_ADDR_TYPE_ANALOG_10BIT:
            data_pool_->setAnalog(analog_idx++, analog10bitToVoltage(word));
            break;
        case ORBITA_ADDR_TYPE_ANALOG_9BIT:
            data_pool_->setAnalog(analog_idx++, analog9bitToVoltage(word));
            break;
        case ORBITA_ADDR_TYPE_CONTACT:
            data_pool_->setContact(contact_idx++, contactExtractBit(word, ch.bitNumber));
            break;
        case ORBITA_ADDR_TYPE_FAST_1:
            data_pool_->setFast(fast_idx++, fastT21Value(word));
            break;
        case ORBITA_ADDR_TYPE_FAST_2: {
            uint32_t idx2 = ch.numOutElemG + ch.stepOutG - 1;
            if (idx2 < word_count) {
                int val = fastT22Value(word, group_words[idx2]);
                data_pool_->setFast(fast_idx++, val);
            } else {
                data_pool_->setFast(fast_idx++, 0);
            }
            break;
        }
        case ORBITA_ADDR_TYPE_FAST_3:
        case ORBITA_ADDR_TYPE_FAST_4: {
            uint32_t idx2 = ch.numOutElemG + ch.stepOutG - 1;
            if (idx2 < word_count) {
                int val = fastT24Value(word, group_words[idx2]);
                data_pool_->setFast(fast_idx++, val);
            } else {
                data_pool_->setFast(fast_idx++, 0);
            }
            break;
        }
        case ORBITA_ADDR_TYPE_TEMPERATURE:
            data_pool_->setTemperature(temp_idx++, temperatureCode(word));
            break;
        case ORBITA_ADDR_TYPE_BUS: {
            uint32_t idx2 = ch.numOutElemG + ch.stepOutG - 1;
            if (idx2 < word_count) {
                uint16_t val = busValue(word, group_words[idx2]);
                data_pool_->setBus(bus_idx++, val);
            } else {
                data_pool_->setBus(bus_idx++, 0);
            }
            break;
        }
        default:
            break;
        }
    }

    // Запись в TLM, если активна
    if (tlm_writer_) {
        // Нужно накапливать 32 группы в цикл – пока упростим: пишем каждую группу как отдельный блок?
        // В реальности TlmWriter ожидает цикл из 32 групп. Для простоты здесь не реализуем,
        // но можно добавить накопление в буфер cycle_buffer_ в классе Context.
        // Пока пропускаем.
    }

    // Сигналим пользователю о новых данных
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        new_data_available_ = true;
    }
    data_cv_.notify_one();
}

// -----------------------------------------------------------------
// Ожидание данных (публичный метод)
// -----------------------------------------------------------------
bool Context::waitForData(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(data_mutex_);
    bool ok = data_cv_.wait_for(lock, timeout, [this] { return new_data_available_.load(); });
    if (ok) {
        new_data_available_ = false;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------
// Доступ к значениям каналов
// -----------------------------------------------------------------
double Context::getAnalog(int idx) const {
    if (!data_pool_) return 0.0;
    return data_pool_->getAnalog(idx);
}

int Context::getContact(int idx) const {
    if (!data_pool_) return 0;
    return data_pool_->getContact(idx);
}

int Context::getFast(int idx) const {
    if (!data_pool_) return 0;
    return data_pool_->getFast(idx);
}

int Context::getTemperature(int idx) const {
    if (!data_pool_) return 0;
    return data_pool_->getTemperature(idx);
}

int Context::getBus(int idx) const {
    if (!data_pool_) return 0;
    return data_pool_->getBus(idx);
}

// -----------------------------------------------------------------
// Статистика ошибок
// -----------------------------------------------------------------
int Context::getPhraseErrorPercent() const {
    if (!decoder_) return 0;
    int phrase_err, group_err;
    decoder_->getErrors(phrase_err, group_err);
    return phrase_err;
}

int Context::getGroupErrorPercent() const {
    if (!decoder_) return 0;
    int phrase_err, group_err;
    decoder_->getErrors(phrase_err, group_err);
    return group_err;
}

// -----------------------------------------------------------------
// Lua (заглушки)
// -----------------------------------------------------------------
void Context::loadScript(const std::string& script_path) {
    // TODO: реализация
    LOG_WARNING("Lua scripting not implemented yet");
}

std::string Context::runScriptFunction(const std::string& func_name, const std::string& args_json) {
    LOG_WARNING("Lua scripting not implemented yet");
    return "{}";
}

} // namespace orbita
