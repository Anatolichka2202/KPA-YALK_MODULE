#include "orbita_stand/parameter_repository.h"
#include <stdexcept>
#include <utility>

namespace orbita::stand {

struct ParameterRepository::Source {
    explicit Source(std::unique_ptr<IParameterSource> value) : device(std::move(value)) {}
    struct Entry {
        ParameterSample sample;
        std::chrono::steady_clock::time_point received;
        std::optional<ParameterSample> lastGood;
    };
    std::unique_ptr<IParameterSource> device;
    std::mutex io;
    mutable std::mutex cacheMutex;
    std::map<std::string, Entry> cache;
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
    {
        std::lock_guard<std::mutex> lock(source->cacheMutex);
        // Отказ опроса трактуется как отключение источника; его остальные
        // параметры тоже больше нельзя показывать как достоверные.
        if (failed || result.quality == SampleQuality::Disconnected) {
            for (auto& item : source->cache) {
                item.second.sample.quality = SampleQuality::Disconnected;
                item.second.sample.diagnostic = result.diagnostic;
            }
        }
        auto& entry = source->cache[key];
        if (result.quality == SampleQuality::Good) {
            // Повтор старого отсчёта не продлевает свежесть даже после сбоя.
            if (!entry.lastGood || result.sequence != entry.lastGood->sequence
                || result.timestamp != entry.lastGood->timestamp)
                entry.received = std::chrono::steady_clock::now();
            entry.lastGood = result;
        }
        entry.sample = result;
    }
    return result;
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
        source->device->safeStop();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        clearing_ = false;
    }
    cleared_.notify_all();
}

} // namespace orbita::stand
