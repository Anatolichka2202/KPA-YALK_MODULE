#include "orbita_stand/parameter_repository.h"
#include <stdexcept>
#include <deque>
#include <limits>
#include <utility>
#include <atomic>
#include <cmath>
#include <thread>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace orbita::stand {

ParameterStreamSubscription::ParameterStreamSubscription(std::size_t capacity, ParameterProvenance provenance)
    : capacity_(capacity), provenance_(provenance)
{
    if (!capacity_) throw std::invalid_argument("Размер очереди записи должен быть больше нуля");
}

bool ParameterStreamSubscription::beginRead()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || closing_) return false;
    ++pendingReads_;
    return true;
}

void ParameterStreamSubscription::finishRead(const std::vector<ParameterStreamSample>& samples, bool failed) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (failed) { overflow_ = true; closing_ = true; }
    if (!overflow_) {
        for (const auto& sample : samples) {
            if (queue_.size() >= capacity_) { overflow_ = true; closing_ = true; break; }
            try { queue_.push_back(sample); }
            catch (...) { overflow_ = true; closing_ = true; break; }
        }
    }
    if (pendingReads_ > 0) --pendingReads_;
    if (closing_ && pendingReads_ == 0) closed_ = true;
}

ParameterStreamBatch ParameterStreamSubscription::take()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ParameterStreamBatch result;
    result.samples.swap(queue_);
    result.overflow = overflow_;
    result.closed = closed_;
    result.sourceStopped = sourceStopped_;
    return result;
}

void ParameterStreamSubscription::close() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    closing_ = true;
    if (pendingReads_ == 0) closed_ = true;
}

void ParameterStreamSubscription::closeFromSource() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!closed_ && !closing_) sourceStopped_ = true;
    closing_ = true;
    if (pendingReads_ == 0) closed_ = true;
}

struct ParameterRepository::Source {
    explicit Source(std::unique_ptr<IParameterSource> value)
        : device(std::move(value)), provenance(device->provenance()) {}
    struct Entry {
        ParameterSample sample;
        std::chrono::steady_clock::time_point received;
        std::optional<ParameterSample> lastGood;
        std::deque<ParameterObservation> observations;
        std::uint64_t revision = 0;
        void append(const ParameterSample& value) {
            observations.push_back({value, std::chrono::steady_clock::now(), ++revision});
            if (observations.size() > 256) observations.pop_front();
        }
    };
    std::unique_ptr<IParameterSource> device;
    const ParameterProvenance provenance;
    std::mutex io;
    mutable std::mutex cacheMutex;
    std::map<std::string, Entry> cache;
    std::mutex subscribersMutex;
    std::vector<std::weak_ptr<ParameterStreamSubscription>> subscribers;
    bool subscriptionsClosed = false; // Защищено subscribersMutex.
    bool closed = false; // Защищено io; закрытый источник больше не опрашивается.
};

ParameterRepository::~ParameterRepository() { clear(); }

void ParameterRepository::add(std::unique_ptr<IParameterSource> source)
{
    if (!source) throw std::invalid_argument("Пустой источник параметров");
    const auto id = source->sourceId();
    if (id.empty()) throw std::invalid_argument("Источник параметров без идентификатора");
    std::lock_guard<std::mutex> lock(mutex_);
    if (clearing_) throw std::runtime_error("Источники останавливаются");
    if (sources_.count(id)) throw std::invalid_argument("Повторный идентификатор источника: " + id);
    sources_.emplace(id, std::make_shared<Source>(std::move(source)));
}

std::shared_ptr<ParameterRepository::Source> ParameterRepository::find(const std::string& id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = sources_.find(id);
    if (it == sources_.end()) throw std::out_of_range("Неизвестный источник параметров: " + id);
    return it->second;
}

ParameterSample ParameterRepository::refresh(const std::string& id, const std::string& key)
{
    if (key.empty()) throw std::invalid_argument("Пустой идентификатор параметра");
    const auto source = find(id);
    std::lock_guard<std::mutex> ioLock(source->io);
    if (source->closed) throw std::runtime_error("Источник параметров закрыт");
    std::vector<std::shared_ptr<ParameterStreamSubscription>> active;
    try {
        {
            std::lock_guard<std::mutex> lock(source->subscribersMutex);
            for (auto it = source->subscribers.begin(); it != source->subscribers.end();) {
                if (auto subscriber = it->lock()) {
                    active.push_back(subscriber);
                    if (!subscriber->beginRead()) active.pop_back();
                    ++it;
                } else {
                    it = source->subscribers.erase(it);
                }
            }
        }
    ParameterSample result;
    bool failed = false;
    try {
        result = source->device->read(key);
        if (result.parameterKey != key) throw std::runtime_error("Источник вернул другой параметр");
    } catch (const std::exception& error) {
        result = {};
        result.parameterKey = key;
        result.quality = SampleQuality::Disconnected;
        result.diagnostic = error.what();
        failed = true;
    }
    const auto receivedAt = std::chrono::system_clock::now();
    const auto receivedSteady = std::chrono::steady_clock::now();
    std::vector<ParameterStreamSample> streamEvents;
    {
        std::lock_guard<std::mutex> lock(source->cacheMutex);
        // Отказ опроса трактуется как отключение источника; его остальные
        // параметры тоже больше нельзя показывать как достоверные.
        if (failed || result.quality == SampleQuality::Disconnected) {
            for (auto& item : source->cache) {
                item.second.sample.quality = SampleQuality::Disconnected;
                item.second.sample.diagnostic = result.diagnostic;
                if (item.first != key) {
                    item.second.append(item.second.sample);
                    streamEvents.push_back({item.second.sample, receivedAt, {}});
                }
            }
        }
        auto& entry = source->cache[key];
        if (result.quality == SampleQuality::Good) {
            // Повтор старого отсчёта не продлевает свежесть даже после сбоя.
            if (!entry.lastGood || result.sequence != entry.lastGood->sequence
                || result.timestamp != entry.lastGood->timestamp)
                entry.received = receivedSteady;
            entry.lastGood = result;
        }
        entry.sample = result;
        entry.append(result);
        streamEvents.push_back({result, receivedAt,
            result.quality == SampleQuality::Good ? receivedSteady - entry.received
                                                   : std::chrono::steady_clock::duration{}});
    }
    // Подписчики получают тот же результат, что и остальные потребители;
    // накопление не обращается к источнику повторно и не выполняет файловый IO.
    for (const auto& subscriber : active) subscriber->finishRead(streamEvents);
    return result;
    } catch (...) {
        // Если публикация не состоялась, файл не должен выглядеть полным.
        for (const auto& subscriber : active)
            subscriber->finishRead({}, true);
        throw;
    }
}

std::shared_ptr<ParameterStreamSubscription> ParameterRepository::subscribeStream(
    const std::string& id, std::size_t capacity)
{
    const auto source = find(id);
    auto subscriber = std::make_shared<ParameterStreamSubscription>(capacity, source->provenance);
    // Подписка не ждёт медленного чтения прибора, выполняющегося в io.
    std::lock_guard<std::mutex> lock(source->subscribersMutex);
    if (source->subscriptionsClosed) throw std::runtime_error("Источник параметров закрыт");
    source->subscribers.push_back(subscriber);
    return subscriber;
}

std::optional<ParameterSample> ParameterRepository::latest(const std::string& id,
    const std::string& key, std::chrono::milliseconds maxAge) const
{
    if (maxAge.count() < 0) throw std::invalid_argument("Отрицательный срок свежести");
    const auto source = find(id);
    std::lock_guard<std::mutex> lock(source->cacheMutex);
    const auto it = source->cache.find(key);
    if (it == source->cache.end()) return std::nullopt;
    auto result = it->second.sample;
    if (result.quality == SampleQuality::Good) {
        const auto wallNow = std::chrono::system_clock::now();
        // Время из источника обязательно: его отсутствие или часы из будущего
        // не позволяют подтвердить свежесть даже у только что полученного значения.
        if (result.timestamp == std::chrono::system_clock::time_point{}
            || result.timestamp > wallNow)
            result.quality = SampleQuality::Invalid;
        else if (wallNow - result.timestamp > maxAge
                 || std::chrono::steady_clock::now() - it->second.received > maxAge)
            result.quality = SampleQuality::Stale;
    }
    return result;
}

ParameterObservationBatch ParameterRepository::observationsAfter(const std::string& id,
    const std::string& key, std::uint64_t revision) const
{
    const auto source = find(id);
    std::lock_guard<std::mutex> lock(source->cacheMutex);
    const auto it = source->cache.find(key);
    if (it == source->cache.end()) return {};
    ParameterObservationBatch batch;
    batch.lastRevision = it->second.revision;
    if (revision == std::numeric_limits<std::uint64_t>::max()) return batch;
    const auto& history = it->second.observations;
    batch.overflow = !history.empty() && revision < history.front().revision - 1;
    for (const auto& observation : history)
        if (observation.revision > revision) batch.observations.push_back(observation);
    return batch;
}

void ParameterRepository::clear() noexcept
{
    // Во время остановки новая регистрация отклоняется, но внешний код
    // источника не вызывается под общей блокировкой репозитория.
    std::map<std::string, std::shared_ptr<Source>> sources;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cleared_.wait(lock, [this] { return !clearing_; });
        clearing_ = true;
        sources.swap(sources_);
    }
    for (const auto& item : sources) {
        const auto& source = item.second;
        std::lock_guard<std::mutex> lock(source->io);
        source->closed = true;
        {
            std::lock_guard<std::mutex> subscribersLock(source->subscribersMutex);
            source->subscriptionsClosed = true;
            for (const auto& weak : source->subscribers)
                if (auto subscriber = weak.lock()) subscriber->closeFromSource();
        }
        source->device->safeStop();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        clearing_ = false;
    }
    cleared_.notify_all();
}

struct ParameterStreamRecording::Impl {
    std::shared_ptr<ParameterStreamSubscription> subscription;
    std::string sourceId;
    std::string filePath;
    std::atomic_bool stopRequested{false};
    mutable std::mutex mutex;
    ParameterRecordingStatus current;
    std::thread worker;

    void setStatus(ParameterRecordingState state, std::string error = {}) {
        std::lock_guard<std::mutex> lock(mutex);
        current.state = state;
        current.error = std::move(error);
    }
    void run() noexcept {
        try {
            QFile file(QString::fromUtf8(filePath.data(), static_cast<int>(filePath.size())));
            if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                throw std::runtime_error(file.errorString().toUtf8().toStdString());
            const auto write = [&file](const QJsonObject& object) {
                auto data = QJsonDocument(object).toJson(QJsonDocument::Compact);
                data.append('\n');
                if (file.write(data) != data.size())
                    throw std::runtime_error(file.errorString().toUtf8().toStdString());
            };
            const auto flush = [&file] {
                if (!file.flush()) throw std::runtime_error(file.errorString().toUtf8().toStdString());
            };
            write({{"type", "header"}, {"schema", 1},
                   {"source_id", QString::fromUtf8(sourceId.data(), static_cast<int>(sourceId.size()))},
                   {"scope", "polled_parameter_samples"},
                   {"provenance", subscription->provenance() == ParameterProvenance::Simulated
                       ? "simulated" : "unknown"}});
            flush();
            setStatus(ParameterRecordingState::Recording);
            for (;;) {
                auto batch = subscription->take();
                for (const auto& observation : batch.samples) {
                    const auto& sample = observation.sample;
                    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        sample.timestamp.time_since_epoch()).count();
                    const auto receivedNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        observation.receivedAt.time_since_epoch()).count();
                    const char* sourceQuality = sample.quality == SampleQuality::Good ? "good"
                        : sample.quality == SampleQuality::Stale ? "stale"
                        : sample.quality == SampleQuality::Disconnected ? "disconnected" : "invalid";
                    const char* quality = sourceQuality;
                    const char* validityReason = "";
                    if (sample.quality == SampleQuality::Good) {
                        // Используем тот же пятисекундный предел, что и монитор;
                        // время сравниваем с получением, а не с задержкой записи.
                        if (sample.timestamp == std::chrono::system_clock::time_point{}
                            || sample.timestamp > observation.receivedAt) {
                            quality = "invalid"; validityReason = "invalid_timestamp";
                        } else if (observation.receivedAt - sample.timestamp > std::chrono::seconds(5)) {
                            quality = "stale"; validityReason = "stale_timestamp";
                        } else if (observation.unchangedFor > std::chrono::seconds(5)) {
                            quality = "stale"; validityReason = "unchanged_sample";
                        } else if (!std::isfinite(sample.rawValue) || !std::isfinite(sample.physicalValue)) {
                            quality = "invalid"; validityReason = "non_finite_value";
                        }
                    }
                    QJsonObject object{{"type", "sample"},
                        {"parameter", QString::fromUtf8(sample.parameterKey.data(), static_cast<int>(sample.parameterKey.size()))},
                        {"timestamp_ns", sample.timestamp == std::chrono::system_clock::time_point{}
                            ? QJsonValue(QJsonValue::Null) : QJsonValue(QString::number(nanos))},
                        {"received_ns", QString::number(receivedNanos)},
                        {"sequence", QString::number(sample.sequence)},
                        {"raw", sample.quality == SampleQuality::Good && std::isfinite(sample.rawValue)
                            ? QJsonValue(sample.rawValue) : QJsonValue(QJsonValue::Null)},
                        {"value", sample.quality == SampleQuality::Good && std::isfinite(sample.physicalValue)
                            ? QJsonValue(sample.physicalValue) : QJsonValue(QJsonValue::Null)},
                        {"unit", QString::fromUtf8(sample.unit.data(), static_cast<int>(sample.unit.size()))},
                        {"quality", quality},
                        {"source_quality", sourceQuality}, {"validity_reason", validityReason},
                        {"diagnostic", QString::fromUtf8(sample.diagnostic.data(), static_cast<int>(sample.diagnostic.size()))}};
                    write(object);
                }
                if (!batch.samples.empty()) {
                    flush();
                    std::lock_guard<std::mutex> lock(mutex);
                    current.samplesWritten += batch.samples.size();
                }
                if (batch.closed) {
                    const bool complete = !batch.overflow && !batch.sourceStopped && stopRequested.load();
                    const std::string reason = batch.overflow ? "Переполнение очереди записи"
                        : complete ? std::string() : "Источник остановлен до завершения записи";
                    write({{"type", "end"}, {"complete", complete}, {"reason", QString::fromUtf8(reason.c_str())}});
                    flush();
                    setStatus(complete ? ParameterRecordingState::Completed : ParameterRecordingState::Failed, reason);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        } catch (const std::exception& error) {
            subscription->close();
            setStatus(ParameterRecordingState::Failed, error.what());
        } catch (...) {
            subscription->close();
            setStatus(ParameterRecordingState::Failed, "Неизвестная ошибка записи");
        }
    }
};

ParameterStreamRecording::ParameterStreamRecording(ParameterRepository& repository,
    std::string sourceId, std::string filePath, std::size_t queueCapacity)
    : impl_(std::make_unique<Impl>())
{
    if (filePath.empty()) throw std::invalid_argument("Не указан файл записи");
    impl_->subscription = repository.subscribeStream(sourceId, queueCapacity);
    impl_->current.provenance = impl_->subscription->provenance();
    impl_->sourceId = std::move(sourceId);
    impl_->filePath = std::move(filePath);
    impl_->worker = std::thread([state = impl_.get()] { state->run(); });
}

ParameterStreamRecording::~ParameterStreamRecording()
{
    requestStop();
    if (impl_->worker.joinable()) impl_->worker.join();
}

void ParameterStreamRecording::requestStop() noexcept
{
    impl_->stopRequested.store(true);
    impl_->subscription->close();
}

ParameterRecordingStatus ParameterStreamRecording::status() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->current;
}

} // namespace orbita::stand
