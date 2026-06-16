#include "orbita_core.h"
#include "../address/address_manager.h"
#include "../address/address_parser.h"
#include "../value/value_converter.h"
#include "../include/orbita_types.h"
#include "logger.h"
#include <algorithm>
#include <chrono>

namespace orbita {

// ------------------------------------------------------------------
//  Извлечение бортового времени (МТВ) из группы.
//  МТВ лежит в маркере кадра (16-я фраза) в формате BCD по 4 декады.
//  Возвращает 0xFFFFFFFF, если группа не содержит маркер кадра.
// ------------------------------------------------------------------
static uint32_t extractSecondsFromGroup(const std::vector<uint16_t>& g) {
    constexpr int WORDS_PER_PHRASE = 16;

    // Первое слово 16-й фразы (индекс 15) — маркер кадра (бит 0x800)
    const size_t frameStart = 15 * WORDS_PER_PHRASE;
    if (frameStart >= g.size())          return 0xFFFFFFFF;
    if ((g[frameStart] & 0x800) == 0)    return 0xFFFFFFFF;

    auto extractDigit = [&](int phraseOffset) -> uint16_t {
        uint16_t val = 0;
        for (int i = 0; i < 4; ++i) {
            size_t idx = static_cast<size_t>(phraseOffset + i * 2) * WORDS_PER_PHRASE;
            if (idx >= g.size()) return 0;
            val = static_cast<uint16_t>((val << 1) | ((g[idx] >> 11) & 1));
        }
        return val;
    };

    auto bcd2int = [](uint16_t bcd) -> int {
        int v = 0;
        for (int i = 0; i < 4; ++i) v = (v << 1) | ((bcd >> (3 - i)) & 1);
        return (v > 9) ? 0 : v;
    };

    return bcd2int(extractDigit(17)) * 1000
         + bcd2int(extractDigit(25)) * 100
         + bcd2int(extractDigit(33)) * 10
         + bcd2int(extractDigit(41));
}

// ------------------------------------------------------------------
//  Конструктор / деструктор
// ------------------------------------------------------------------
Context::Context()
    : recoverer_(bitFifo_, BitstreamRecoverer::NORMAL)
{
    start_time_ = std::chrono::steady_clock::now();
    LOG_INFO("Orbita context created");
}

Context::~Context() {
    stop();
    LOG_INFO("Orbita context destroyed");
}

// ------------------------------------------------------------------
//  Декодер
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

// ------------------------------------------------------------------
//  Источник данных
// ------------------------------------------------------------------
void Context::setDeviceE2010(int channel, double rate_khz) {
    auto dev = std::make_unique<E2010Device>();
    if (!dev->init(0, channel, rate_khz))
        throw orbita_error("E2010 init failed");
    device_ = std::move(dev);
    LOG_INFO("Device E20-10 set (channel %d, %.0f kHz)", channel, rate_khz);
}

void Context::setDeviceNone() {
    device_.reset();
    LOG_INFO("Device: none (decode-only mode)");
}

// ------------------------------------------------------------------
//  Каналы (горячая замена)
// ------------------------------------------------------------------
void Context::setChannels(const std::vector<ChannelSpec>& specs) {
    std::vector<ChannelDesc> descs;
    std::vector<std::string> addrs, names, cats;
    descs.reserve(specs.size());

    for (const auto& s : specs) {
        try {
            ChannelDesc d = AddressParser::parseLine(s.address);
            d.userName = s.name;
            d.category = s.category;
            descs.push_back(std::move(d));
            addrs.push_back(s.address);
            names.push_back(s.name);
            cats.push_back(s.category);
        } catch (const std::exception& e) {
            LOG_WARNING("setChannels: parse error for '%s': %s",
                        s.address.c_str(), e.what());
        }
    }

    // finalize() заполняет wordIndex / groups / cycles, сохраняя порядок и число
    AddressManager mgr(informativnost_);
    mgr.addChannels(descs);
    mgr.finalize();

    ensureDecoder();
    auto snap = registry_.update(mgr.getChannels(), addrs, names, cats);

    {
        std::lock_guard<std::mutex> lock(values_mutex_);
        values_.assign(snap->entries.size(), 0.0);
        valid_.assign(snap->entries.size(), 0);
    }
    LOG_INFO("setChannels: %zu channels active", snap->entries.size());
}

std::vector<ChannelSpec> Context::getChannels() const {
    std::vector<ChannelSpec> result;
    auto cfg = registry_.get();
    if (!cfg) return result;
    result.reserve(cfg->entries.size());
    for (const auto& e : cfg->entries)
        result.push_back(ChannelSpec{e.address, e.name, e.category});
    return result;
}

// ------------------------------------------------------------------
//  Жизненный цикл
// ------------------------------------------------------------------
void Context::start() {
    if (is_running_.load()) throw orbita_error("Already running");
    if (!registry_.hasChannels())
        throw orbita_error("No channels configured. Call setChannels() first.");

    ensureDecoder();
    stop_worker_ = false;
    paused_ = false;
    samples_processed_ = 0;
    start_time_ = std::chrono::steady_clock::now();

    if (device_) {
        device_->setSamplesCallback([this](const std::vector<int16_t>& s) {
            pushToQueue(s);
        });
        device_->setErrorCallback([](const std::string& msg) {
            LOG_ERROR("Device error: %s", msg.c_str());
        });
        if (!device_->start())
            throw orbita_error("Failed to start device");
    } else {
        LOG_INFO("Starting in decode-only mode (no device)");
    }

    decoder_thread_ = std::thread(&Context::decoderLoop, this);
    is_running_ = true;
    LOG_INFO("Orbita started");
}

void Context::stop() {
    if (!is_running_.load()) return;

    if (device_) device_->stop();   // больше колбэков не будет

    stop_worker_ = true;
    queue_cv_.notify_all();
    if (decoder_thread_.joinable()) decoder_thread_.join();

    is_running_ = false;
    LOG_INFO("Orbita stopped");
}

void Context::pause() {
    paused_ = true;
    LOG_INFO("Orbita paused (values frozen)");
}

void Context::resume() {
    paused_ = false;
    LOG_INFO("Orbita resumed");
}

void Context::setInvertSignal(bool invert) {
    invert_signal_ = invert;
    LOG_INFO("Signal inversion: %s", invert ? "ON" : "OFF");
}

// ------------------------------------------------------------------
//  Запись TLM (делегируем декодеру)
// ------------------------------------------------------------------
void Context::startRecording(const std::string& filename) {
    if (decoder_) decoder_->startTlm(filename);
    else LOG_WARNING("startRecording: decoder not ready");
}

void Context::stopRecording() {
    if (decoder_) decoder_->stopTlm();
}

bool Context::isRecording() const {
    return decoder_ && decoder_->isTlmActive();
}

// ------------------------------------------------------------------
//  Очередь отсчётов
// ------------------------------------------------------------------
void Context::pushToQueue(const std::vector<int16_t>& samples) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    sample_queue_.push(samples);
    queue_cv_.notify_one();
}

// ------------------------------------------------------------------
//  Декодерный поток
// ------------------------------------------------------------------
void Context::decoderLoop() {
    bool threshold_computed = false;
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

        samples_processed_ += samples.size();
    }

    recoverer_.flush();
    if (decoder_) decoder_->process();
    LOG_INFO("Decoder thread finished. Total: %llu samples",
             (unsigned long long)samples_processed_.load());
}

// ------------------------------------------------------------------
//  Колбэк декодера: готова одна группа (2048 слов)
// ------------------------------------------------------------------
void Context::onDecoderGroup(const std::vector<uint16_t>& group12,
                             int groupNum, int ciklNum)
{
    if (paused_.load()) return;

    // 1) Бортовое время — независимо от наличия каналов
    const uint32_t sec = extractSecondsFromGroup(group12);
    if (sec != 0xFFFFFFFF) current_mtv_.store(sec);

    // 2) Каналы (если заданы)
    auto cfg = registry_.get();
    Snapshot pushSnap;
    bool haveValues = false;

    {
        std::lock_guard<std::mutex> lock(values_mutex_);
        // Конфиг мог смениться между get() и блокировкой — пропускаем каналы,
        // но МТВ всё равно обновится (см. ниже)
        if (cfg && values_.size() == cfg->entries.size() && !cfg->entries.empty()) {
            for (size_t e = 0; e < cfg->entries.size(); ++e) {
                const ChannelDesc& ch = cfg->entries[e].desc;

                if (!ch.groups.empty() &&
                    std::find(ch.groups.begin(), ch.groups.end(), groupNum) == ch.groups.end())
                    continue;
                if (!ch.cycles.empty() &&
                    std::find(ch.cycles.begin(), ch.cycles.end(), ciklNum) == ch.cycles.end())
                    continue;

                int idx = ch.wordIndex;
                if (idx < 0 || idx >= static_cast<int>(group12.size())) continue;

                uint16_t w  = group12[idx];
                uint16_t w2 = (idx + 1 < static_cast<int>(group12.size()))
                                  ? group12[idx + 1] : 0;
                double v;

                switch (ch.adressType) {
                case ORBITA_ADDR_TYPE_ANALOG_10BIT: v = analog10bitCode(w);              break;
                case ORBITA_ADDR_TYPE_ANALOG_9BIT:  v = analog9bitCode(w);               break;
                case ORBITA_ADDR_TYPE_CONTACT:      v = contactExtractBit(w, ch.bitNumber); break;
                case ORBITA_ADDR_TYPE_FAST_1:       v = fastT21Value(w);                 break;
                case ORBITA_ADDR_TYPE_FAST_2:       v = fastT22Value(w, w2);             break;
                case ORBITA_ADDR_TYPE_FAST_4:       v = fastT24Value(w, w2);             break;
                case ORBITA_ADDR_TYPE_TEMPERATURE:  v = temperatureCode(w);              break;
                case ORBITA_ADDR_TYPE_BUS:          v = busValue(w, w2);                 break;
                case ORBITA_ADDR_TYPE_FAST_3:       v = (w & 0x0FFF);                    break; // T23: сырое слово
                default: continue;
                }

                values_[e] = v;
                valid_[e]  = 1;
            }
            fillValuesLocked(*cfg, pushSnap);
            haveValues = true;
        }
    }

    // 3) Сигнализируем UI на каждой группе — чтобы МТВ тикал даже без каналов
    {
        std::lock_guard<std::mutex> lk(data_mutex_);
        data_updated_ = true;
    }
    data_cv_.notify_one();

    // 4) Push-колбэк
    DataCallback cb;
    { std::lock_guard<std::mutex> lk(callback_mutex_); cb = data_callback_; }
    if (cb) {
        if (!haveValues) { /* МТВ-only снапшот */ }
        pushSnap.mtv_seconds = current_mtv_.load();
        pushSnap.stats       = getStats();
        cb(pushSnap);
    }
}

// ------------------------------------------------------------------
//  Доступ к данным
// ------------------------------------------------------------------
bool Context::waitForData(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(data_mutex_);
    bool ok = data_cv_.wait_for(lock, timeout,
                                [this] { return data_updated_.load(); });
    if (ok) data_updated_ = false;
    return ok;
}

void Context::fillValuesLocked(const ChannelRegistry::Snapshot& cfg, Snapshot& out) const {
    out.values.resize(cfg.entries.size());
    for (size_t i = 0; i < cfg.entries.size(); ++i) {
        out.values[i].address = cfg.entries[i].address;
        out.values[i].value   = values_[i];
        out.values[i].valid   = valid_[i] != 0;
    }
}

Snapshot Context::getSnapshot() const {
    Snapshot snap;
    auto cfg = registry_.get();
    if (cfg) {
        std::lock_guard<std::mutex> lock(values_mutex_);
        if (values_.size() == cfg->entries.size())
            fillValuesLocked(*cfg, snap);
    }
    snap.mtv_seconds = getCurrentTimeSeconds();
    snap.stats       = getStats();
    return snap;
}

std::optional<double> Context::getValueByAddress(const std::string& address) const {
    auto cfg = registry_.get();
    if (!cfg) return std::nullopt;
    auto it = cfg->addrIndex.find(address);
    if (it == cfg->addrIndex.end()) return std::nullopt;

    std::lock_guard<std::mutex> lock(values_mutex_);
    if (it->second >= values_.size())  return std::nullopt;
    if (!valid_[it->second])           return std::nullopt;
    return values_[it->second];
}

Stats Context::getStats() const {
    Stats s;
    if (decoder_) {
        int pe = 0, ge = 0;
        decoder_->getErrors(pe, ge);
        s.phrase_error_percent = pe;
        s.group_error_percent  = ge;
        s.frames_processed     = static_cast<uint64_t>(decoder_->getTotalGroups());
    }
    double secs = std::chrono::duration<double>(
                      std::chrono::steady_clock::now() - start_time_).count();
    if (secs > 0.0)
        s.mb_per_second = static_cast<double>(samples_processed_.load())
                          * sizeof(int16_t) / 1e6 / secs;
    return s;
}

uint32_t Context::getCurrentTimeSeconds() const {
    return current_mtv_.load();
}

void Context::setDataCallback(DataCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    data_callback_ = std::move(cb);
}

} // namespace orbita
