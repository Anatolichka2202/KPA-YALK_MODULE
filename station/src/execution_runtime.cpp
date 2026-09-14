#include "orbita_stand/execution_runtime.h"
#include "orbita_stand/config.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace orbita::stand {
namespace {

int integerValue(const std::map<std::string, std::string>& configuration,
                 const std::string& key, int fallback)
{
    const auto iterator = configuration.find(key);
    if (iterator == configuration.end() || iterator->second.empty()) return fallback;
    return std::stoi(iterator->second);
}

std::string stringValue(const std::map<std::string, std::string>& configuration,
                        const std::string& key)
{
    const auto iterator = configuration.find(key);
    return iterator == configuration.end() ? std::string{} : iterator->second;
}

class ExternalProcessRuntime final : public IExecutionRuntime {
public:
    explicit ExternalProcessRuntime(std::map<std::string, std::string> configuration)
        : configuration_(std::move(configuration))
        , program_(stringValue(configuration_, "program"))
        , defaultEntrypoint_(stringValue(configuration_, "entrypoint"))
        , defaultWorkingDirectory_(stringValue(configuration_, "working_directory"))
        , defaultTimeoutMs_(integerValue(configuration_, "timeout_ms", 0))
        , startTimeoutMs_(integerValue(configuration_, "start_timeout_ms", 5000))
        , terminateGraceMs_(integerValue(configuration_, "terminate_grace_ms", 1000))
    {
        if (defaultTimeoutMs_ < 0 || startTimeoutMs_ <= 0 || terminateGraceMs_ < 0) {
            throw std::invalid_argument("Invalid external process timeout configuration");
        }
        if (program_.empty() && defaultEntrypoint_.empty()) {
            // Допускается runtime без заранее закреплённого executable, если
            // target будет передан при execute().
        }
    }

    ExecutionResult execute(const ExecutionRequest& request) override
    {
        if (running_.exchange(true)) {
            throw std::runtime_error("Execution runtime is already busy");
        }
        struct RunningGuard {
            std::atomic_bool& flag;
            ~RunningGuard() { flag = false; }
        } guard{running_};

        cancelRequested_ = false;
        ExecutionResult result;

        const std::string target = request.target.empty()
            ? defaultEntrypoint_ : request.target;

        std::string executable;
        QStringList arguments;
        if (!program_.empty()) {
            executable = program_;
            if (!target.empty()) arguments.push_back(QString::fromUtf8(target));
        } else {
            executable = target;
        }
        if (executable.empty()) {
            throw std::invalid_argument(
                "Execution runtime requires program or request/default target");
        }
        for (const auto& argument : request.arguments) {
            arguments.push_back(QString::fromUtf8(argument));
        }

        QProcess process;
        const std::string workingDirectory = request.workingDirectory.empty()
            ? defaultWorkingDirectory_ : request.workingDirectory;
        if (!workingDirectory.empty()) {
            process.setWorkingDirectory(QString::fromUtf8(workingDirectory));
        }

        auto environment = QProcessEnvironment::systemEnvironment();
        for (const auto& [key, value] : request.environment) {
            environment.insert(QString::fromUtf8(key), QString::fromUtf8(value));
        }
        process.setProcessEnvironment(environment);
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start(QString::fromUtf8(executable), arguments);

        if (!process.waitForStarted(startTimeoutMs_)) {
            result.standardError = process.errorString().toUtf8().toStdString();
            return result;
        }
        result.started = true;

        const int timeoutMs = request.timeoutMs > 0
            ? request.timeoutMs : defaultTimeoutMs_;
        QElapsedTimer elapsed;
        elapsed.start();

        auto drainOutput = [&] {
            result.standardOutput += process.readAllStandardOutput().toStdString();
            result.standardError += process.readAllStandardError().toStdString();
        };

        while (process.state() != QProcess::NotRunning) {
            process.waitForFinished(50);
            drainOutput();

            const bool cancelled = cancelRequested_.load();
            const bool timedOut = timeoutMs > 0 && elapsed.elapsed() >= timeoutMs;
            if (!cancelled && !timedOut) continue;

            result.cancelled = cancelled;
            result.timedOut = timedOut;
            process.terminate();
            if (!process.waitForFinished(terminateGraceMs_)) {
                process.kill();
                process.waitForFinished(std::max(1000, terminateGraceMs_));
            }
            break;
        }

        drainOutput();
        if (process.state() == QProcess::NotRunning) {
            result.exitCode = process.exitCode();
        }
        return result;
    }

    void cancel() noexcept override
    {
        cancelRequested_ = true;
    }

    bool isRunning() const noexcept override
    {
        return running_.load();
    }

private:
    std::map<std::string, std::string> configuration_;
    std::string program_;
    std::string defaultEntrypoint_;
    std::string defaultWorkingDirectory_;
    int defaultTimeoutMs_ = 0;
    int startTimeoutMs_ = 5000;
    int terminateGraceMs_ = 1000;
    std::atomic_bool running_{false};
    std::atomic_bool cancelRequested_{false};
};

} // namespace

std::unique_ptr<IExecutionRuntime> createExecutionRuntime(
    const std::string& provider,
    const std::map<std::string, std::string>& configuration)
{
    if (provider == "miltech.exec.process") {
        return std::make_unique<ExternalProcessRuntime>(configuration);
    }
    throw std::invalid_argument("Unknown execution-runtime provider: " + provider);
}

void registerExecutionRuntimeComponents(ComponentRuntime& runtime)
{
    runtime.registerKindFactory("execution_runtime",
        [](const ComponentProfile& definition) -> std::unique_ptr<IStationComponent> {
            return createExecutionRuntime(definition.provider, definition.configuration);
        });
}

} // namespace orbita::stand
