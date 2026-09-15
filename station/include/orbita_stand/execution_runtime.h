#pragma once

#include "orbita_stand/component_runtime.h"

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace orbita::stand {

struct ExecutionRequest {
    std::string target;
    std::vector<std::string> arguments;
    std::map<std::string, std::string> environment;
    std::string workingDirectory;
    int timeoutMs = 0; // 0 = no overall timeout
};

struct ExecutionResult {
    int exitCode = -1;
    bool started = false;
    bool cancelled = false;
    bool timedOut = false;
    std::string standardOutput;
    std::string standardError;
};

// Common contract for existing Python/Lua/native bench software. Providers may
// execute differently, while Station sees one lifecycle and one result shape.
class IExecutionRuntime : public IStationComponent {
public:
    ~IExecutionRuntime() override = default;

    std::string_view componentKind() const noexcept final
    {
        return "execution_runtime";
    }

    void safeStop() noexcept final
    {
        cancel();
    }

    virtual ExecutionResult execute(const ExecutionRequest& request) = 0;
    virtual void cancel() noexcept = 0;
    virtual bool isRunning() const noexcept = 0;
};

std::unique_ptr<IExecutionRuntime> createExecutionRuntime(
    const std::string& provider,
    const std::map<std::string, std::string>& configuration);

void registerExecutionRuntimeComponents(ComponentRuntime& runtime);

class ScenarioEngine;

// Generic scenario action `station.execute`. It translates step arguments to
// ExecutionRequest and stores process outcome/stdout/stderr as run evidence.
void registerExecutionProcedure(ScenarioEngine& engine, IExecutionRuntime& runtime);

} // namespace orbita::stand
