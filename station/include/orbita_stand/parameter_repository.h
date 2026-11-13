#pragma once

#include "orbita_stand/telemetry.h"
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

namespace orbita::stand {

struct ParameterObservation {
    ParameterSample sample;
    std::chrono::steady_clock::time_point received;
    std::uint64_t revision = 0;
};

struct ParameterObservationBatch {
    std::vector<ParameterObservation> observations;
    std::uint64_t lastRevision = 0;
    bool overflow = false;
};

// Сессия владеет источниками. Опрос выполняется одним владельцем вне GUI,
// а наблюдение и проверка читают один и тот же последний снимок без повторного
// обращения к прибору. Идентичность параметра включает идентификатор источника.
class ParameterRepository final {
public:
    ~ParameterRepository();
    ParameterRepository() = default;
    ParameterRepository(const ParameterRepository&) = delete;
    ParameterRepository& operator=(const ParameterRepository&) = delete;

    void add(std::unique_ptr<IParameterSource> source);
    // Отключение источника помечает его параметры недоступными;
    // снимки других источников не изменяются.
    ParameterSample refresh(const std::string& sourceId, const std::string& parameterKey);
    std::optional<ParameterSample> latest(const std::string& sourceId,
        const std::string& parameterKey, std::chrono::milliseconds maxAge) const;
    // Курсор UINT64_MAX возвращает только текущую ревизию для начала наблюдения.
    ParameterObservationBatch observationsAfter(const std::string& sourceId,
        const std::string& parameterKey, std::uint64_t revision) const;
    void clear() noexcept;
private:
    struct Source;
    std::shared_ptr<Source> find(const std::string& sourceId) const;
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Source>> sources_;
    bool clearing_ = false;
    std::condition_variable cleared_;
};

} // namespace orbita::stand
