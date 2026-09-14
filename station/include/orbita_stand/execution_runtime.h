#pragma once

#include "orbita_stand/component_runtime.h"

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace orbita::stand {

struct ExecutionRequest {
    // Для interpreter-style runtime это путь к скрипту/entrypoint. Если target
    // пуст, provider может использовать entrypoint из своего профиля.
    std::string target;
    std::vector<std::string> arguments;
    std::map<std::string, std::string> environment;
    std::string workingDirectory;
    int timeoutMs = 0; // 0 = без общего timeout
};

struct ExecutionResult {
    int exitCode = -1;
    bool started = false;
    bool cancelled = false;
    bool timedOut = false;
    std::string standardOutput;
    std::string standardError;
};

// Унифицированная точка запуска существующих программ/скриптов внутри run
// станции. Lua/Python/native/external-process providers могут иметь разные
// реализации, но Station и сценарий видят один lifecycle/result contract.
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

} // namespace orbita::stand
