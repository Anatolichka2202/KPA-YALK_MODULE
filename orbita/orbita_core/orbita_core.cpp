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
//  Возвращает пустое значение, если маркер отсутствует или повреждён.
// ------------------------------------------------------------------
static std::optional<uint32_t> extractSecondsFromGroup(const std::vector<uint16_t>& g) {
    constexpr int WORDS_PER_PHRASE = 16;

    // Первое слово 16-й фразы (индекс 15) — маркер кадра (бит 0x800)
    const size_t frameStart = 15 * WORDS_PER_PHRASE;
    constexpr auto lastTimeWord = (41 + 3 * 2) * WORDS_PER_PHRASE;
    if (g.size() <= lastTimeWord || (g[frameStart] & 0x800) == 0) return std::nullopt;

    auto extractDigit = [&](int phraseOffset) -> uint16_t {
        uint16_t val = 0;
        for (int i = 0; i < 4; ++i) {
            size_t idx = static_cast<size_t>(phraseOffset + i * 2) * WORDS_PER_PHRASE;
            val = static_cast<uint16_t>((val << 1) | ((g[idx] >> 11) & 1));
        }
        return val;
    };

    uint32_t seconds = 0;
    for (const int phrase : {17, 25, 33, 41}) {
        const auto digit = extractDigit(phrase);
        // Некорректная декада не должна превращаться в ноль бортового времени.
        if (digit > 9) return std::nullopt;
        seconds = seconds * 10 + digit;
    }
    return seconds;
}

// ------------------------------------------------------------------
//  Конструктор / деструктор
// ------------------------------------------------------------------
Context::Context(Informativity format, double samplesPerSecond)
    : recoverer_(bitFifo_, BitstreamRecoverer::NORMAL),
      informativnost_(static_cast<int>(format))
{
    if (format == Informativity::M8) {
        auto decoder = std::make_unique<FrameDecoderM8>(samplesPerSecond);
        m8Decoder_ = decoder.get();
        decoder_ = std::move(decoder);
    } else if (format == Informativity::M16) {
        decoder_ = std::make_unique<FrameDecoderM16>(bitFifo_);
    } else {
        throw orbita_error("Неподдерживаемая информативность Орбита");
    }
    decoder_->setGroupCallback([this](const auto& words, int group, int cycle) {
        onDecoderGroup(words, group, cycle);
    });
    start_time_ = std::chrono::steady_clock::now();
    LOG_INFO("Orbita context created");
}

Context::~Context() {
    stop();
    LOG_INFO("Orbita context destroyed");
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
            const std::string prefix = m8Decoder_ ? "08" : "16";
            if (s.address.substr(1, 2) != prefix)
                throw std::invalid_argument("Информативность адреса отличается от режима декодера");
            // В UnitMoth нет работающей привязки к абсолютному номеру
            // цикла кадра. Адрес с периодом больше цикла нельзя читать по
            // относительному номеру, иначе можно выдать значение чужого канала.
            if (m8Decoder_ && (d.numOutElemG == 0 || d.numOutElemG > d.stepOutG
                || d.stepOutG > 32 * 1024))
                throw std::invalid_argument("Адрес M8 выходит за подтверждённую адресацию цикла");
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

    auto snap = registry_.update(mgr.getChannels(), addrs, names, cats);

    {
        std::lock_guard<std::mutex> lock(values_mutex_);
        values_.assign(snap->entries.size(), 0.0);
        valid_.assign(snap->entries.size(), 0);
        updatedAt_.assign(snap->entries.size(), {});
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
//  Жизненный цикл декодера
// ------------------------------------------------------------------
void Context::start() {
    if (is_running_.load()) throw orbita_error("Already running");
    if (!registry_.hasChannels()) {
        // Кадровой анализатор читает группы целиком и может работать без
        // подписки на последний отсчёт отдельных каналов.
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (!group_callback_)
            throw orbita_error("No channels or group callback configured.");
    }

    // Новый сеанс не должен декодировать очередь, незавершённый кадр или
    // последнее значение от предыдущего источника.
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        std::queue<std::vector<int16_t>> empty;
        sample_queue_.swap(empty);
        queued_count_ = 0;
        processed_count_ = 0;
    }
    bitFifo_.reset();
    recoverer_.reset();
    {
        std::lock_guard<std::recursive_mutex> lock(decoder_mutex_);
        decoder_->reset();
        publishDecoderStats();
    }
    {
        std::lock_guard<std::mutex> lock(values_mutex_);
        std::fill(values_.begin(), values_.end(), 0.0);
        std::fill(valid_.begin(), valid_.end(), 0);
        std::fill(updatedAt_.begin(), updatedAt_.end(), std::chrono::steady_clock::time_point{});
    }
    data_updated_.store(false);
    current_mtv_.store(noBoardTime);
    mtv_received_ticks_.store(0);
    stop_worker_ = false;
    paused_ = false;
    samples_processed_ = 0;
    start_time_ = std::chrono::steady_clock::now();

    decoder_thread_ = std::thread(&Context::decoderLoop, this);
    is_running_ = true;
    LOG_INFO("Orbita decoder started; raw samples are supplied through pushSamples()");
}

void Context::stopDiscardPending() {
    if (!is_running_.load()) return;
    // Called only after the station sample producer has stopped. A recording
    // requires the lossless drain path, even if fast-stop was requested.
    if (isRecording()) {
        stop();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        std::queue<std::vector<int16_t>> empty;
        sample_queue_.swap(empty);
        processed_count_ = queued_count_;
    }
    processed_cv_.notify_all();
    stop();
}

void Context::stop() {
    if (!is_running_.load()) return;

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
    std::lock_guard<std::recursive_mutex> lock(decoder_mutex_);
    if (!decoder_ || !is_running_) throw orbita_error("Декодер не запущен");
    decoder_->startTlm(filename);
}

void Context::stopRecording() {
    // Сначала завершаем обработку отсчётов, уже переданных источником в момент
    // нажатия «Стоп записи»; поступившие позже могут остаться вне записи.
    if (decoder_thread_.joinable() && std::this_thread::get_id() != decoder_thread_.get_id()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        const auto target = queued_count_;
        processed_cv_.wait(lock, [this, target] { return processed_count_ >= target; });
    }
    std::lock_guard<std::recursive_mutex> lock(decoder_mutex_);
    if (decoder_) decoder_->stopTlm();
}

bool Context::isRecording() const {
    std::lock_guard<std::recursive_mutex> lock(decoder_mutex_);
    return decoder_ && decoder_->isTlmActive();
}

// ------------------------------------------------------------------
//  Очередь отсчётов
// ------------------------------------------------------------------
void Context::pushToQueue(std::vector<int16_t>&& samples) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (stop_worker_) return;
    sample_queue_.push(std::move(samples));
    ++queued_count_;
    queue_cv_.notify_one();
}

// ------------------------------------------------------------------
//  Декодерный поток
// ------------------------------------------------------------------
void Context::decoderLoop() {
    bool m16Inverted = invert_signal_.load();
    recoverer_.setPolarity(m16Inverted ? BitstreamRecoverer::INVERTED : BitstreamRecoverer::NORMAL);
    LOG_INFO("Decoder thread started");

    while (true) {
        std::vector<int16_t> samples;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (!queue_cv_.wait_for(lock, std::chrono::milliseconds(100),
                    [this] { return !sample_queue_.empty() || stop_worker_.load(); }))
                continue;
            // После остановки источника обрабатываем принятую очередь.
            if (sample_queue_.empty() && stop_worker_) break;
            if (sample_queue_.empty()) continue;
            samples = std::move(sample_queue_.front());
            sample_queue_.pop();
        }

        if (m8Decoder_) {
            std::lock_guard<std::recursive_mutex> lock(decoder_mutex_);
            m8Decoder_->appendSamples(samples, invert_signal_.load());
            decoder_->process();
            publishDecoderStats();
        } else {
            const bool inverted = invert_signal_.load();
            if (inverted != m16Inverted) {
                m16Inverted = inverted;
                std::lock_guard<std::recursive_mutex> lock(decoder_mutex_);
                bitFifo_.reset();
                recoverer_.reset();
                recoverer_.setPolarity(inverted ? BitstreamRecoverer::INVERTED : BitstreamRecoverer::NORMAL);
                decoder_->reset();
            }
            // В ACPUnit.pas порог обновляется по каждому большому буферу
            // АЦП. Ровный начальный блок не задаёт порог на весь сеанс.
            if (!recoverer_.isThresholdComputed() || samples.size() >= 100000)
                recoverer_.computeThreshold(samples.data(), samples.size(), 100000);
            recoverer_.processSamples(samples.data(), samples.size());
            std::lock_guard<std::recursive_mutex> lock(decoder_mutex_);
            decoder_->process();
            publishDecoderStats();
        }

        samples_processed_ += samples.size();
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            ++processed_count_;
        }
        processed_cv_.notify_all();
    }

    if (!m8Decoder_) {
        recoverer_.flush();
        std::lock_guard<std::recursive_mutex> lock(decoder_mutex_);
        decoder_->process();
        publishDecoderStats();
    }
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
    if (!m8Decoder_) {
        if (const auto seconds = extractSecondsFromGroup(group12)) {
            current_mtv_.store(*seconds);
            mtv_received_ticks_.store(std::chrono::steady_clock::now().time_since_epoch().count());
        }
    }

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
                case ORBITA_ADDR_TYPE_CONTACT:
                    // В БРП T05 без Pxx обозначает весь пакет из восьми
                    // контактных битов (разряды 10:3 слова M16).
                    v = !m8Decoder_ && ch.bitNumber == 0 ? (w >> 3) & 0xff
                                                       : contactExtractBit(w, ch.bitNumber);
                    break;
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
                updatedAt_[e] = std::chrono::steady_clock::now();
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
    GroupCallback groupCb;
    {
        std::lock_guard<std::mutex> lk(callback_mutex_);
        cb = data_callback_;
        groupCb = group_callback_;
    }
    if (groupCb) groupCb(group12, groupNum, ciklNum);
    if (cb) {
        publishDecoderStats(); // Вызывается из process() под блокировкой декодера.
        if (!haveValues) { /* МТВ-only снапшот */ }
        pushSnap.mtv_seconds = getCurrentTimeSeconds();
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
        out.values[i].updatedAt = updatedAt_[i];
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

void Context::publishDecoderStats() {
    Stats s;
    if (decoder_) {
        int pe = 0, ge = 0;
        decoder_->getErrors(pe, ge);
        s.phrase_error_percent = pe;
        s.group_error_percent  = ge;
        s.frames_processed     = static_cast<uint64_t>(decoder_->getTotalGroups());
    }
    std::lock_guard<std::mutex> lock(decoderStatsMutex_);
    decoderStats_ = s;
}

Stats Context::getStats() const {
    Stats s;
    {
        // GUI читает опубликованный срез и не ждёт обработки блока АЦП.
        std::lock_guard<std::mutex> lock(decoderStatsMutex_);
        s = decoderStats_;
    }
    double secs = std::chrono::duration<double>(
                      std::chrono::steady_clock::now() - start_time_).count();
    if (secs > 0.0)
        s.mb_per_second = static_cast<double>(samples_processed_.load())
                          * sizeof(int16_t) / 1e6 / secs;
    return s;
}

uint32_t Context::getCurrentTimeSeconds() const {
    return getBoardTimeSeconds().value_or(0);
}

std::optional<uint32_t> Context::getBoardTimeSeconds() const {
    const auto time = current_mtv_.load();
    if (time == noBoardTime) return std::nullopt;
    return time;
}

bool Context::isBoardTimeFresh(std::chrono::milliseconds maxAge) const {
    using Clock = std::chrono::steady_clock;
    const auto ticks = mtv_received_ticks_.load();
    return ticks != 0 && Clock::now() - Clock::time_point{Clock::duration{ticks}} <= maxAge;
}

void Context::setDataCallback(DataCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    data_callback_ = std::move(cb);
}

void Context::setGroupCallback(GroupCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    group_callback_ = std::move(cb);
}

} // namespace orbita
