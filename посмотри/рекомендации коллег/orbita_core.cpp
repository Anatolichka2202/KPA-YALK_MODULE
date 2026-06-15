#include "orbita_core.h"
#include "orbita.h"
#include "address/address_manager.h"
#include "address/address_parser.h"
#include "data_pool.h"
#include "logger.h"
#include "orbita_types.h"
#include "value/value_converter.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace orbita {

// ─────────────────────────────────────────────────────────────────────────────
// Вспомогательная: извлечение бортового времени (МТВ) из группы
// Алгоритм не трогаем — он рабочий.
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t extractSecondsFromGroup(const std::vector<uint16_t>& g) {
    constexpr int WORDS_PER_PHRASE = 16;

    // Первое слово 16-й фразы (индекс 15) — маркер кадра
    const size_t frameStart = 15 * WORDS_PER_PHRASE;
    if (frameStart >= g.size()) return 0xFFFFFFFF;
    if ((g[frameStart] & 0x800) == 0) return 0xFFFFFFFF;

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

    uint32_t sec = bcd2int(extractDigit(17)) * 1000
                 + bcd2int(extractDigit(25)) * 100
                 + bcd2int(extractDigit(33)) * 10
                 + bcd2int(extractDigit(41));
    return sec;
}

// ─────────────────────────────────────────────────────────────────────────────
Context::Context()
    : recoverer_(bitFifo_, BitstreamRecoverer::NORMAL)
{
    data_pool_ = std::make_unique<DataPool>();

    // Декодер создаём в конструкторе, а не в setChannels* —
    // это позволяет вызвать start() без предварительной загрузки каналов.
    decoder_ = std::make_unique<FrameDecoderM16>(bitFifo_);
    decoder_->setGroupCallback(
        [this](const auto& /*frame*/, const auto& group12, int gn, int cn) {
            onDecoderGroup(group12, gn, cn);
        });

    LOG_INFO("Orbita context created");
}

Context::~Context() {
    stop();
    LOG_INFO("Orbita context destroyed");
}

// ─────────────────────────────────────────────────────────────────────────────
// Устройство
// ─────────────────────────────────────────────────────────────────────────────
void Context::setDeviceE2010(int channel, double sampleRateKHz) {
    auto dev = std::make_unique<E2010Device>();
    if (!dev->init(0, channel, sampleRateKHz))
        throw orbita_error("E2010 init failed");
    device_ = std::move(dev);
    LOG_INFO("E2010Device init OK: ch=%d rate=%.0f kHz", channel, sampleRateKHz);
}

// ─────────────────────────────────────────────────────────────────────────────
// Каналы
// ─────────────────────────────────────────────────────────────────────────────
void Context::applyChannels(std::vector<ChannelDesc> descs,
                             const std::vector<std::string>& names,
                             const std::vector<std::string>& cats)
{
    // Финализация: AddressManager вычисляет wordIndex, groups, cycles
    AddressManager mgr(informativnost_);
    mgr.addChannels(descs);
    mgr.finalize();
    const auto& final_descs = mgr.getChannels();

    // Атомарная замена в registry_ + вычисление poolIndex
    auto snap = registry_.update(final_descs, names, cats);

    // Изменяем размер DataPool под новую конфигурацию
    // DataPool внутри потокобезопасен (std::mutex на каждом vector),
    // но resize лучше вызывать когда декодер не в mid-group.
    // На практике достаточно: resize обнуляет данные, что допустимо.
    data_pool_->resize(snap->analogCount, snap->contactCount,
                       snap->fastCount, snap->tempCount, snap->busCount);

    LOG_INFO("Channels applied: analog=%d contact=%d fast=%d temp=%d bus=%d",
             snap->analogCount, snap->contactCount,
             snap->fastCount, snap->tempCount, snap->busCount);
}

void Context::setChannelsFromFile(const std::string& filename) {
    AddressManager tmp(informativnost_);
    tmp.loadFromFile(filename);
    applyChannels(tmp.getChannels());
}

void Context::setChannelsFromLines(const std::vector<std::string>& lines) {
    AddressManager tmp(informativnost_);
    tmp.loadFromLines(lines);
    applyChannels(tmp.getChannels());
}

void Context::setChannelsFromPairs(
    const std::vector<std::pair<std::string,std::string>>& pairs)
{
    std::vector<ChannelDesc> descs;
    std::vector<std::string> names;
    descs.reserve(pairs.size());
    names.reserve(pairs.size());

    for (const auto& [addr, name] : pairs) {
        try {
            ChannelDesc d = AddressParser::parseLine(addr);
            d.userName = name;
            descs.push_back(d);
            names.push_back(name);
        } catch (const std::exception& e) {
            LOG_WARNING("Parse error '%s': %s", addr.c_str(), e.what());
        }
    }

    if (!descs.empty()) applyChannels(std::move(descs), names);
}

// ─────────────────────────────────────────────────────────────────────────────
// Управление сбором
// ─────────────────────────────────────────────────────────────────────────────
void Context::start() {
    if (is_running_) throw orbita_error("Already running");

    stop_worker_ = false;

    if (device_) {
        // Устанавливаем колбэки ДО start() устройства
        device_->setSamplesCallback([this](const std::vector<int16_t>& s) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            sample_queue_.push(s);
            queue_cv_.notify_one();
        });
        device_->setErrorCallback([](const std::string& msg) {
            LOG_ERROR("Device: %s", msg.c_str());
        });
        if (!device_->start())
            throw orbita_error("Failed to start device stream");
    } else {
        LOG_INFO("Starting in raw-data mode (no device)");
    }

    decoder_thread_ = std::thread(&Context::decoderLoop, this);
    is_running_ = true;

    LOG_INFO("Orbita started (channels configured: %s)",
             registry_.hasChannels() ? "yes" : "no — ждём setChannels*");
}

void Context::stop() {
    if (!is_running_) return;

    stop_worker_ = true;
    queue_cv_.notify_all();

    if (decoder_thread_.joinable()) decoder_thread_.join();
    if (device_) device_->stop();

    is_running_ = false;
    LOG_INFO("Orbita stopped");
}

void Context::setInvertSignal(bool /*invert*/) {
    // TODO: recoverer_.setPolarity(invert ? INVERTED : NORMAL);
    LOG_WARNING("setInvertSignal not yet implemented");
}

void Context::feedRawData(const int16_t* samples, size_t count) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    sample_queue_.push(std::vector<int16_t>(samples, samples + count));
    queue_cv_.notify_one();
}

// ─────────────────────────────────────────────────────────────────────────────
// TLM
// ─────────────────────────────────────────────────────────────────────────────
void Context::startRecording(const std::string& filename) {
    tlm_writer_ = std::make_unique<TlmWriter>(filename, informativnost_);
    LOG_INFO("Recording: %s", filename.c_str());
}

void Context::stopRecording() {
    tlm_writer_.reset();
    LOG_INFO("Recording stopped");
}

// ─────────────────────────────────────────────────────────────────────────────
// Поток декодера
// ─────────────────────────────────────────────────────────────────────────────
void Context::decoderLoop() {
    bool   threshold_computed = false;
    size_t samples_total      = 0;

    LOG_INFO("Decoder thread started");

    while (!stop_worker_) {
        std::vector<int16_t> samples;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !sample_queue_.empty() || stop_worker_.load();
            });
            if (stop_worker_) break;
            if (sample_queue_.empty()) continue;
            samples = std::move(sample_queue_.front());
            sample_queue_.pop();
        }

        // Вычисляем порог один раз по первым 100 000 отсчётам
        if (!threshold_computed && samples.size() >= 100000) {
            recoverer_.computeThreshold(samples.data(), samples.size(), 100000);
            threshold_computed = true;
        }

        recoverer_.processSamples(samples.data(), samples.size());
        decoder_->process();  // сам вызовет groupCallback при готовности группы

        samples_total += samples.size();
        if (samples_total % 5'000'000 == 0)
            LOG_INFO("Decoder: processed %zu M samples", samples_total / 1'000'000);
    }

    recoverer_.flush();
    decoder_->process();
    LOG_INFO("Decoder thread finished. Total: %zu samples", samples_total);
}

// ─────────────────────────────────────────────────────────────────────────────
// Колбэк декодера — вызывается из decoderLoop
// ИСПРАВЛЕНО: используем entry.poolIndex вместо глобального i
// ─────────────────────────────────────────────────────────────────────────────
void Context::onDecoderGroup(const std::vector<uint16_t>& group12,
                              int groupNum, int ciklNum) {
    // Бортовое время — независимо от наличия каналов
    const uint32_t sec = extractSecondsFromGroup(group12);
    if (sec != 0xFFFFFFFF) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (sec != current_mtv_) {
            current_mtv_ = sec;
        }
    }

    // Берём снапшот: один lock, без блокировки на время итерации
    auto snap = registry_.get();
    if (!snap || snap->entries.empty()) {
        // Каналы ещё не заданы — обновляем только МТВ
        data_updated_ = true;
        data_cv_.notify_one();
        return;
    }

    bool anyUpdated = false;

    for (const auto& entry : snap->entries) {
        const ChannelDesc& ch = entry.desc;
        const int pi          = entry.poolIndex;  // ← правильный per-type индекс

        if (pi < 0) continue;  // неизвестный тип

        // Фильтрация по группе и циклу
        if (!ch.groups.empty() &&
            std::find(ch.groups.begin(), ch.groups.end(),
                      static_cast<uint16_t>(groupNum)) == ch.groups.end())
            continue;
        if (!ch.cycles.empty() &&
            std::find(ch.cycles.begin(), ch.cycles.end(),
                      static_cast<uint16_t>(ciklNum)) == ch.cycles.end())
            continue;

        const int idx = ch.wordIndex;
        if (idx < 0 || idx >= static_cast<int>(group12.size())) continue;

        const uint16_t word = group12[idx];

        switch (ch.adressType) {
        case ORBITA_ADDR_TYPE_ANALOG_10BIT:
            data_pool_->setAnalog(pi, static_cast<double>(analog10bitCode(word)));
            anyUpdated = true;
            break;

        case ORBITA_ADDR_TYPE_ANALOG_9BIT:
            data_pool_->setAnalog(pi, static_cast<double>(analog9bitCode(word)));
            anyUpdated = true;
            break;

        case ORBITA_ADDR_TYPE_CONTACT:
            data_pool_->setContact(pi, contactExtractBit(word, ch.bitNumber));
            anyUpdated = true;
            break;

        case ORBITA_ADDR_TYPE_FAST_1:
            data_pool_->setFast(pi, fastT21Value(word));
            anyUpdated = true;
            break;

        case ORBITA_ADDR_TYPE_FAST_2:
        case ORBITA_ADDR_TYPE_FAST_3:
        case ORBITA_ADDR_TYPE_FAST_4:
            // Двухсловные типы: второе слово = следующий wordIndex
            if (ch.width >= 2 && idx + 1 < static_cast<int>(group12.size())) {
                data_pool_->setFast(pi, fastT22Value(word, group12[idx + 1]));
                anyUpdated = true;
            }
            break;

        case ORBITA_ADDR_TYPE_TEMPERATURE:
            data_pool_->setTemperature(pi, temperatureCode(word));
            anyUpdated = true;
            break;

        case ORBITA_ADDR_TYPE_BUS:
            // БУС — два слова, старший байт в word, младший в следующем
            if (ch.width >= 2 && idx + 1 < static_cast<int>(group12.size())) {
                data_pool_->setBus(pi, static_cast<int>(busValue(word, group12[idx + 1])));
                anyUpdated = true;
            }
            break;

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

// ─────────────────────────────────────────────────────────────────────────────
bool Context::waitForData(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(data_mutex_);
    bool ok = data_cv_.wait_for(lock, timeout,
                                [this] { return data_updated_.load(); });
    if (ok) data_updated_ = false;
    return ok;
}

uint32_t Context::getCurrentTimeSeconds() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return current_mtv_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Геттеры значений
// ─────────────────────────────────────────────────────────────────────────────
double Context::getAnalog     (int i) const { return data_pool_->getAnalog(i); }
int    Context::getContact    (int i) const { return data_pool_->getContact(i); }
int    Context::getFast       (int i) const { return data_pool_->getFast(i); }
int    Context::getTemperature(int i) const { return data_pool_->getTemperature(i); }
int    Context::getBus        (int i) const { return data_pool_->getBus(i); }
int    Context::getAnalogCount () const { return data_pool_->analogCount(); }
int    Context::getContactCount() const { return data_pool_->contactCount(); }

std::string Context::getChannelName(int analogIdx) const {
    auto snap = registry_.get();
    if (!snap) return {};
    int cnt = 0;
    for (const auto& e : snap->entries) {
        if (e.desc.adressType == ORBITA_ADDR_TYPE_ANALOG_10BIT ||
            e.desc.adressType == ORBITA_ADDR_TYPE_ANALOG_9BIT) {
            if (cnt++ == analogIdx) return e.name;
        }
    }
    return {};
}

std::string Context::getChannelCategory(int analogIdx) const {
    auto snap = registry_.get();
    if (!snap) return {};
    int cnt = 0;
    for (const auto& e : snap->entries) {
        if (e.desc.adressType == ORBITA_ADDR_TYPE_ANALOG_10BIT ||
            e.desc.adressType == ORBITA_ADDR_TYPE_ANALOG_9BIT) {
            if (cnt++ == analogIdx) return e.category;
        }
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Статистика
// ─────────────────────────────────────────────────────────────────────────────
int Context::getPhraseErrorPercent() const {
    if (!decoder_) return 0;
    int pe = 0, ge = 0;
    decoder_->getErrors(pe, ge);
    return pe;
}

int Context::getGroupErrorPercent() const {
    if (!decoder_) return 0;
    int pe = 0, ge = 0;
    decoder_->getErrors(pe, ge);
    return ge;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua-заглушки
// ─────────────────────────────────────────────────────────────────────────────
void        Context::loadScript       (const std::string&) { LOG_WARNING("Lua not implemented"); }
std::string Context::runScriptFunction(const std::string&,
                                        const std::string&) { return "{}"; }

} // namespace orbita
