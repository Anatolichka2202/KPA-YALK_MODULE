#include "orbita_core.h"
#include "../include/orbita.h"
#include "../address/address_manager.h"
#include "data_pool.h"
#include "../value/value_converter.h"
#include "logger.h"
#include "../include/orbita_types.h"
#include <algorithm>
#include <chrono>
#include <thread>

namespace orbita {

// ------------------------------------------------------------------
// Конструктор / деструктор
// ------------------------------------------------------------------
Context::Context()
    : recoverer_(bitFifo_, BitstreamRecoverer::NORMAL)
{
    data_pool_ = std::make_unique<DataPool>();
    LOG_INFO("Orbita context created");
}

Context::~Context() {
    stop();
    LOG_INFO("Orbita context destroyed");
}

// ------------------------------------------------------------------
// Вспомогательные: декодер и конфиг
// ------------------------------------------------------------------
void Context::ensureDecoder() {
    if (decoder_) return;
    decoder_ = std::make_unique<FrameDecoderM16>(bitFifo_);
    decoder_->setGroupCallback(
        [this](const std::vector<uint16_t>& /*group11*/,
               const std::vector<uint16_t>& group12,
               int groupNum, int ciklNum) {
            onDecoderGroup(group12, groupNum, ciklNum);
        });
}

void Context::applyChannels(std::vector<ChannelDesc> descs) {
    // Назначаем poolIndex — порядковый индекс внутри каждого типа
    int analog_cnt = 0, contact_cnt = 0, fast_cnt = 0, temp_cnt = 0, bus_cnt = 0;
    for (auto& ch : descs) {
        switch (ch.adressType) {
        case ORBITA_ADDR_TYPE_ANALOG_10BIT:
        case ORBITA_ADDR_TYPE_ANALOG_9BIT:
            ch.poolIndex = analog_cnt++;   break;
        case ORBITA_ADDR_TYPE_CONTACT:
            ch.poolIndex = contact_cnt++;  break;
        case ORBITA_ADDR_TYPE_FAST_1:
        case ORBITA_ADDR_TYPE_FAST_2:
        case ORBITA_ADDR_TYPE_FAST_3:
        case ORBITA_ADDR_TYPE_FAST_4:
            ch.poolIndex = fast_cnt++;     break;
        case ORBITA_ADDR_TYPE_TEMPERATURE:
            ch.poolIndex = temp_cnt++;     break;
        case ORBITA_ADDR_TYPE_BUS:
            ch.poolIndex = bus_cnt++;      break;
        default:
            break;
        }
    }

    std::lock_guard<std::mutex> lock(channels_mutex_);
    channels_ = std::move(descs);
    data_pool_->resize(analog_cnt, contact_cnt, fast_cnt, temp_cnt, bus_cnt);
    LOG_INFO("Config applied: %d analog, %d contact, %d fast, %d temp, %d bus",
             analog_cnt, contact_cnt, fast_cnt, temp_cnt, bus_cnt);
}

// ------------------------------------------------------------------
// Загрузка адресов
// ------------------------------------------------------------------
void Context::setChannelsFromFile(const std::string& filename) {
    AddressManager addrMgr(informativnost_);
    addrMgr.loadFromFile(filename);
    ensureDecoder();
    applyChannels(addrMgr.getChannels());
}

void Context::setChannelsFromLines(const std::vector<std::string>& lines) {
    AddressManager addrMgr(informativnost_);
    addrMgr.loadFromLines(lines);
    ensureDecoder();
    applyChannels(addrMgr.getChannels());
}

void Context::setChannelsFromPairs(
    const std::vector<std::pair<std::string, std::string>>& addressNamePairs)
{
    std::vector<ChannelDesc> descs;
    descs.reserve(addressNamePairs.size());
    for (const auto& p : addressNamePairs) {
        try {
            ChannelDesc desc = AddressParser::parseLine(p.first);
            desc.userName = p.second;
            descs.push_back(std::move(desc));
        } catch (const std::exception& e) {
            LOG_WARNING("Parse error for '%s': %s", p.first.c_str(), e.what());
        }
    }
    AddressManager addrMgr(informativnost_);
    addrMgr.addChannels(descs);
    addrMgr.finalize();
    ensureDecoder();
    applyChannels(addrMgr.getChannels());
}

// ------------------------------------------------------------------
// Управление сбором
// ------------------------------------------------------------------
void Context::setDeviceE2010(int channel, double sample_rate_khz) {
    auto dev = std::make_unique<E2010Device>();
    if (!dev->init(0, channel, sample_rate_khz))
        throw orbita_error("E2010 init failed");
    device_ = std::move(dev);
}

void Context::start() {
    if (is_running_) throw orbita_error("Already running");
    if (!decoder_)   throw orbita_error("No channels configured. Call setChannels* first.");

    stop_worker_ = false;

    if (device_) {
        // Колбэк вызывается из потока устройства — потокобезопасно через очередь
        device_->setSamplesCallback([this](const std::vector<int16_t>& s) {
            pushToQueue(s);
        });
        device_->setErrorCallback([](const std::string& msg) {
            LOG_ERROR("Device error: %s", msg.c_str());
        });
        if (!device_->start())
            throw orbita_error("Failed to start device");
    } else {
        LOG_INFO("Starting in feed mode (no device)");
    }

    decoder_thread_ = std::thread(&Context::decoderLoop, this);
    is_running_ = true;
    LOG_INFO("Orbita started");
}

void Context::stop() {
    if (!is_running_) return;

    // Сначала останавливаем устройство — больше колбэков не будет
    if (device_) device_->stop();

    stop_worker_ = true;
    queue_cv_.notify_all();
    if (decoder_thread_.joinable()) decoder_thread_.join();

    is_running_ = false;
    LOG_INFO("Orbita stopped");
}

void Context::setInvertSignal(bool invert) {
    invert_signal_ = invert;
    // BitstreamRecoverer не имеет метода setPolarity — пересоздаём при следующем старте
    LOG_INFO("Signal inversion: %s", invert ? "ON" : "OFF");
}

// ------------------------------------------------------------------
// TLM-запись — делегируем декодеру, он владеет TlmWriter
// ------------------------------------------------------------------
void Context::startRecording(const std::string& filename) {
    if (decoder_) decoder_->startTlm(filename);
    else LOG_WARNING("startRecording: decoder not ready");
}

void Context::stopRecording() {
    if (decoder_) decoder_->stopTlm();
}

// ------------------------------------------------------------------
// Инъекция сырых данных (воспроизведение из файла)
// ------------------------------------------------------------------
void Context::feedRawData(const int16_t* samples, size_t count) {
    pushToQueue(std::vector<int16_t>(samples, samples + count));
}

void Context::pushToQueue(const std::vector<int16_t>& samples) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    sample_queue_.push(samples);
    queue_cv_.notify_one();
}

// ------------------------------------------------------------------
// Декодерный поток
// ------------------------------------------------------------------
void Context::decoderLoop() {
    bool threshold_computed = false;
    size_t samples_processed = 0;
    LOG_INFO("Decoder thread started");

    while (!stop_worker_) {
        std::vector<int16_t> samples;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (!queue_cv_.wait_for(lock, std::chrono::milliseconds(100),
                    [this] { return !sample_queue_.empty() || stop_worker_.load(); }))
                continue;
            if (stop_worker_) break;
            samples = std::move(sample_queue_.front());
            sample_queue_.pop();
        }

        if (!threshold_computed && samples.size() >= 100000) {
            recoverer_.computeThreshold(samples.data(), samples.size(), 100000);
            threshold_computed = true;
        }

        recoverer_.processSamples(samples.data(), samples.size());
        decoder_->process();

        samples_processed += samples.size();
        if (samples_processed % 1'000'000 == 0)
            LOG_INFO("Decoder: %zu Msamples processed", samples_processed / 1'000'000);
    }

    recoverer_.flush();
    decoder_->process();
    LOG_INFO("Decoder thread finished. Total: %zu samples", samples_processed);
}

// ------------------------------------------------------------------
// Колбэк декодера: готова одна группа (2048 слов)
// ------------------------------------------------------------------
void Context::onDecoderGroup(const std::vector<uint16_t>& group12,
                             int groupNum, int ciklNum)
{
    // Держим channels_mutex_ на всё время обработки: гарантирует, что
    // applyChannels() (включая data_pool_->resize()) не запустится одновременно
    // с нашими вызовами data_pool_->setAnalog/setContact и т.д.
    std::lock_guard<std::mutex> lock(channels_mutex_);

    bool anyUpdated = false;
    for (const auto& ch : channels_) {
        // Проверяем принадлежность группы/цикла к данному каналу
        if (!ch.groups.empty() &&
            std::find(ch.groups.begin(), ch.groups.end(), groupNum) == ch.groups.end())
            continue;
        if (!ch.cycles.empty() &&
            std::find(ch.cycles.begin(), ch.cycles.end(), ciklNum) == ch.cycles.end())
            continue;

        int idx = ch.wordIndex;
        if (idx < 0 || idx >= static_cast<int>(group12.size())) continue;
        if (ch.poolIndex < 0) continue;

        uint16_t word = group12[idx];

        switch (ch.adressType) {
        case ORBITA_ADDR_TYPE_ANALOG_10BIT:
            data_pool_->setAnalog(ch.poolIndex, static_cast<double>(analog10bitCode(word)));
            anyUpdated = true;
            break;
        case ORBITA_ADDR_TYPE_ANALOG_9BIT:
            data_pool_->setAnalog(ch.poolIndex, static_cast<double>(analog9bitCode(word)));
            anyUpdated = true;
            break;
        case ORBITA_ADDR_TYPE_CONTACT:
            data_pool_->setContact(ch.poolIndex, contactExtractBit(word, ch.bitNumber));
            anyUpdated = true;
            break;
        case ORBITA_ADDR_TYPE_FAST_1:
            data_pool_->setFast(ch.poolIndex, fastT21Value(word));
            anyUpdated = true;
            break;
        // FAST_2, FAST_3, FAST_4, TEMPERATURE, BUS — будет позже
        default:
            break;
        }
    }

    if (anyUpdated) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        data_updated_ = true;
        data_cv_.notify_one();
    }
}

// ------------------------------------------------------------------
// Ожидание данных
// ------------------------------------------------------------------
bool Context::waitForData(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(data_mutex_);
    bool ok = data_cv_.wait_for(lock, timeout,
                                [this] { return data_updated_.load(); });
    if (ok) data_updated_ = false;
    return ok;
}

uint32_t Context::getCurrentTimeSeconds() const {
    // Берём время напрямую из декодера — он его уже извлёк из МТВ
    if (decoder_) return decoder_->getCurrentTimeSeconds();
    return 0;
}

// ------------------------------------------------------------------
// Доступ к каналам
// ------------------------------------------------------------------
double Context::getAnalog(int idx) const      { return data_pool_->getAnalog(idx); }
int    Context::getContact(int idx) const     { return data_pool_->getContact(idx); }
int    Context::getFast(int idx) const        { return data_pool_->getFast(idx); }
int    Context::getTemperature(int idx) const { return data_pool_->getTemperature(idx); }
int    Context::getBus(int idx) const         { return data_pool_->getBus(idx); }
int    Context::getAnalogCount() const        { return data_pool_->analogCount(); }
int    Context::getContactCount() const       { return data_pool_->contactCount(); }

std::string Context::getAnalogChannelName(int idx) const {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    for (const auto& ch : channels_) {
        if ((ch.adressType == ORBITA_ADDR_TYPE_ANALOG_10BIT ||
             ch.adressType == ORBITA_ADDR_TYPE_ANALOG_9BIT) &&
            ch.poolIndex == idx)
            return ch.userName;
    }
    return {};
}

// ------------------------------------------------------------------
// Статистика
// ------------------------------------------------------------------
int Context::getPhraseErrorPercent() const {
    if (!decoder_) return 0;
    int pe, ge;
    decoder_->getErrors(pe, ge);
    return pe;
}

int Context::getGroupErrorPercent() const {
    if (!decoder_) return 0;
    int pe, ge;
    decoder_->getErrors(pe, ge);
    return ge;
}

// ------------------------------------------------------------------
// Lua-заглушки
// ------------------------------------------------------------------
void Context::loadScript(const std::string&) {
    LOG_WARNING("Lua not implemented");
}
std::string Context::runScriptFunction(const std::string&, const std::string&) {
    return "{}";
}

} // namespace orbita
