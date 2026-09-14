#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace orbita::stand {

// Потоковый источник сырых 16-битных отсчётов принадлежит станции, а не
// декодеру конкретного протокола. liborbita является потребителем такого
// потока и не должна знать, E20-10 это, replay-файл или другой АЦП.
class ISampleSource {
public:
    using SamplesCallback = std::function<void(const std::vector<int16_t>&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    virtual ~ISampleSource() = default;

    virtual bool open() = 0;
    virtual void close() noexcept = 0;
    virtual bool isOpen() const noexcept = 0;

    virtual bool start() = 0;
    virtual void stop() noexcept = 0;
    virtual bool isRunning() const noexcept = 0;

    virtual void setSamplesCallback(SamplesCallback callback) = 0;
    virtual void setErrorCallback(ErrorCallback callback) = 0;
};

// Фабрика station-level providers. На первом этапе поддерживается E20-10;
// provider-id берётся из ComponentProfile поставки. По мере появления новых
// источников они добавляются сюда либо переводятся на отдельный plugin ABI для
// потоковых компонентов без изменения liborbita.
std::unique_ptr<ISampleSource> createSampleSource(
    const std::string& provider,
    const std::map<std::string, std::string>& configuration);

} // namespace orbita::stand
