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

struct ParameterStreamSample {
    ParameterSample sample;
    std::chrono::system_clock::time_point receivedAt;
    std::chrono::steady_clock::duration unchangedFor{};
};
struct ParameterStreamBatch {
    std::vector<ParameterStreamSample> samples;
    bool overflow = false;
    bool closed = false;
    bool sourceStopped = false;
};

// Очередь получает фактически прочитанные отсчёты одного источника. При
// переполнении потребитель обязан сообщить о потере, а не выдавать файл за
// полную запись. Закрытие останавливает приём, но оставляет очередь для слива.
class ParameterStreamSubscription final {
public:
    explicit ParameterStreamSubscription(std::size_t capacity,
        ParameterProvenance provenance = ParameterProvenance::Unknown);
    ParameterStreamBatch take();
    void close() noexcept;
    ParameterProvenance provenance() const noexcept { return provenance_; }
private:
    friend class ParameterRepository;
    bool beginRead();
    void finishRead(const std::vector<ParameterStreamSample>& samples, bool failed = false) noexcept;
    void closeFromSource() noexcept;
    std::mutex mutex_;
    std::vector<ParameterStreamSample> queue_;
    std::size_t capacity_;
    ParameterProvenance provenance_;
    bool overflow_ = false;
    bool closed_ = false;
    bool closing_ = false;
    std::size_t pendingReads_ = 0;
    bool sourceStopped_ = false;
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
    std::shared_ptr<ParameterStreamSubscription> subscribeStream(
        const std::string& sourceId, std::size_t capacity = 4096);
    void clear() noexcept;
private:
    struct Source;
    std::shared_ptr<Source> find(const std::string& sourceId) const;
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Source>> sources_;
    bool clearing_ = false;
    std::condition_variable cleared_;
};

enum class ParameterRecordingState { Starting, Recording, Completed, Failed };
struct ParameterRecordingStatus {
    ParameterRecordingState state = ParameterRecordingState::Starting;
    std::uint64_t samplesWritten = 0;
    std::string error;
    ParameterProvenance provenance = ParameterProvenance::Unknown;
};

// Файл пишет отдельный поток. requestStop() не ждёт диска; владелец может
// наблюдать status() и уничтожить объект после перехода в конечное состояние.
class ParameterStreamRecording final {
public:
    ParameterStreamRecording(ParameterRepository& repository, std::string sourceId,
                             std::string filePath, std::size_t queueCapacity = 4096);
    ~ParameterStreamRecording();
    ParameterStreamRecording(const ParameterStreamRecording&) = delete;
    ParameterStreamRecording& operator=(const ParameterStreamRecording&) = delete;
    void requestStop() noexcept;
    ParameterRecordingStatus status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orbita::stand
