#include "parameter_sources.h"
#include "orbita.h"
#include "orbita_stand/sample_source.h"
#include <algorithm>
#include <chrono>
#include <utility>

namespace {
class OrbitaParameters final : public orbita::stand::IParameterSource {
public:
    OrbitaParameters(orbita::Orbita& decoder, orbita::stand::ISampleSource& source,
                     std::shared_mutex& channelsMutex)
        : decoder_(decoder), source_(source), channelsMutex_(channelsMutex), monotonicOrigin_(std::chrono::steady_clock::now()),
          wallOrigin_(std::chrono::system_clock::now()) {}
    std::string sourceId() const override { return "orbita"; }
    std::string probe() override { return source_.isRunning() ? "ready" : "disconnected"; }
    orbita::stand::ParameterSample read(const std::string& key) override {
        orbita::stand::ParameterSample sample;
        sample.parameterKey = key;
        if (!source_.isRunning() || !decoder_.isRunning()) {
            sample.quality = orbita::stand::SampleQuality::Disconnected;
            sample.diagnostic = "Сбор Орбиты не запущен";
            return sample;
        }
        std::shared_lock<std::shared_mutex> lock(channelsMutex_);
        const auto snapshot = decoder_.getSnapshot();
        const auto found = std::find_if(snapshot.values.begin(), snapshot.values.end(),
            [&](const auto& value) { return value.address == key; });
        if (found == snapshot.values.end() || !found->valid
            || found->updatedAt == std::chrono::steady_clock::time_point{}) return sample;
        // Постоянная пара опорных часов сохраняет одинаковую метку времени
        // одного отсчёта при повторном чтении и не продлевает его свежесть.
        sample.timestamp = wallOrigin_ + (found->updatedAt - monotonicOrigin_);
        sample.sequence = static_cast<std::uint64_t>(found->updatedAt.time_since_epoch().count());
        sample.rawValue = found->value;
        sample.physicalValue = found->value;
        sample.unit = "код";
        sample.quality = orbita::stand::SampleQuality::Good;
        return sample;
    }
    void safeStop() noexcept override {} // Физический источник останавливает сессия.
private:
    orbita::Orbita& decoder_;
    orbita::stand::ISampleSource& source_;
    std::shared_mutex& channelsMutex_;
    std::chrono::steady_clock::time_point monotonicOrigin_;
    std::chrono::system_clock::time_point wallOrigin_;
};

class ParameterSimulator final : public orbita::stand::IParameterSource {
public:
    ParameterSimulator(std::string id, std::string key, double value, std::string unit)
        : id_(std::move(id)), key_(std::move(key)), value_(value), unit_(std::move(unit)) {}
    std::string sourceId() const override { return id_; }
    std::string probe() override { return "ready"; }
    orbita::stand::ParameterSample read(const std::string& key) override {
        orbita::stand::ParameterSample sample;
        sample.parameterKey = key;
        if (key != key_) return sample;
        sample.timestamp = std::chrono::system_clock::now();
        sample.sequence = ++sequence_;
        sample.rawValue = value_;
        sample.physicalValue = value_;
        sample.unit = unit_;
        sample.quality = orbita::stand::SampleQuality::Good;
        return sample;
    }
    void safeStop() noexcept override {}
private:
    std::string id_, key_;
    double value_;
    std::string unit_;
    std::uint64_t sequence_ = 0;
};
}

std::unique_ptr<orbita::stand::IParameterSource> makeOrbitaParameters(
    orbita::Orbita& decoder, orbita::stand::ISampleSource& source, std::shared_mutex& channelsMutex)
{
    return std::make_unique<OrbitaParameters>(decoder, source, channelsMutex);
}
std::unique_ptr<orbita::stand::IParameterSource> makeParameterSimulator(
    std::string id, std::string key, double value, std::string unit)
{
    return std::make_unique<ParameterSimulator>(std::move(id), std::move(key), value, std::move(unit));
}
