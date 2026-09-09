#pragma once

#include "orbita_stand/scenario.h"
#include "orbita_stand/telemetry.h"

#include <memory>
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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class RunArtifacts final {
public:
    RunArtifacts(std::string rootDirectory, std::string runId);
    void appendTelemetry(const ParameterSample& sample);
    void appendRawPacket(const std::vector<std::uint8_t>& bytes);
    const std::string& directory() const noexcept;

private:
    std::string directory_;
    std::string telemetryPath_;
    std::string rawPath_;
};

} // namespace orbita::stand
