#pragma once

#include "orbita_stand/scenario.h"
#include "orbita_stand/telemetry.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace orbita::stand {

class RunStore final {
public:
    explicit RunStore(std::string sqlitePath);
    ~RunStore();
    RunStore(const RunStore&) = delete;
    RunStore& operator=(const RunStore&) = delete;

    void save(const ScenarioRunResult& run);
    // Фиксирует промежуточное состояние или завершает ранее начатый запуск.
    // Незавершённый снимок не может перезаписать уже завершённый результат.
    void saveCheckpoint(const ScenarioRunResult& run);

    // Reconstructs the durable run model used by reports and engineering
    // viewers. A missing run has no exceptional meaning and returns nullopt;
    // malformed persisted verdict/attribute data is reported as an error.
    std::optional<ScenarioRunResult> load(const std::string& runId) const;
    std::vector<ScenarioRunResult> unfinishedRuns() const;

private:
    void saveImpl(const ScenarioRunResult& run, bool replaceIncomplete);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Создаёт соединение SQLite в том потоке, где вызывается callback, и
// записывает только промежуточные снимки. Итог фиксирует вызывающая сторона
// после добавления контекста конкретной поставки.
std::function<void(const ScenarioRunResult&)> makeIntermediateCheckpointSink(std::string sqlitePath);

class RunArtifacts final {
public:
    RunArtifacts(std::string rootDirectory, std::string runId);
    void appendTelemetry(const ParameterSample& sample);
    void appendRawPacket(const std::vector<std::uint8_t>& bytes);
    std::vector<ArtifactReference> references() const;
    void attachTo(ScenarioRunResult& run) const;
    void attachFileTo(ScenarioRunResult& run, std::string kind,
                      std::string relativePath, std::string mediaType) const;
    const std::string& directory() const noexcept;

private:
    std::string runId_;
    std::string directory_;
    std::string telemetryPath_;
    std::string rawPath_;

    ArtifactReference referenceFile(std::string kind, std::string relativePath,
                                    std::string mediaType) const;
};

} // namespace orbita::stand
