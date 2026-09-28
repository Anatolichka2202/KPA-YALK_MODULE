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

    // Reconstructs the durable run model used by reports and engineering
    // viewers. A missing run has no exceptional meaning and returns nullopt;
    // malformed persisted verdict/attribute data is reported as an error.
    std::optional<ScenarioRunResult> load(const std::string& runId) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

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
