#include "orbita_stand/execution_runtime.h"
#include "orbita_stand/config.h"
#include "orbita_stand/scenario.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace orbita::stand {
namespace {

constexpr std::size_t kEvidenceLimitBytes = 64 * 1024;

int integerValue(
    const std::map<std::string, std::string>& values,
    const std::string& key,
    int fallback)
{
    const auto found = values.find(key);
    if (found == values.end() || found->second.empty()) return fallback;
    std::size_t parsed = 0;
    const int value = std::stoi(found->second, &parsed);
    if (parsed != found->second.size()) {
        throw std::invalid_argument("Invalid integer value: " + key);
    }
    return value;
}

std::string stringValue(
    const std::map<std::string, std::string>& values,
    const std::string& key)
{
    const auto found = values.find(key);
    return found == values.end() ? std::string{} : found->second;
}

class ExternalProcessRuntime final : public IExecutionRuntime {
public:
    explicit ExternalProcessRuntime(std::map<std::string, std::string> configuration)
        : program_(stringValue(configuration, "program"))
        , defaultEntrypoint_(stringValue(configuration, "entrypoint"))
        , defaultWorkingDirectory_(stringValue(configuration, "working_directory"))
        , defaultTimeoutMs_(integerValue(configuration, "timeout_ms", 0))
        , startTimeoutMs_(integerValue(configuration, "start_timeout_ms", 5000))
        , terminateGraceMs_(integerValue(configuration, "terminate_grace_ms", 1000))
    {
        if (defaultTimeoutMs_ < 0 || startTimeoutMs_ <= 0 || terminateGraceMs_ < 0) {
            throw std::invalid_argument("Invalid external process timeout configuration");
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
            if (!target.empty()) arguments.push_back(QString::fromStdString(target));
        } else {
            executable = target;
        }
        if (executable.empty()) {
            throw std::invalid_argument(
                "Execution runtime requires program or request/default target");
        }
        for (const auto& argument : request.arguments) {
            arguments.push_back(QString::fromStdString(argument));
        }

        QProcess process;
        const std::string workingDirectory = request.workingDirectory.empty()
            ? defaultWorkingDirectory_ : request.workingDirectory;
        if (!workingDirectory.empty()) {
            process.setWorkingDirectory(QString::fromStdString(workingDirectory));
        }

        auto environment = QProcessEnvironment::systemEnvironment();
        for (const auto& [key, value] : request.environment) {
            environment.insert(QString::fromStdString(key), QString::fromStdString(value));
        }
        process.setProcessEnvironment(environment);
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start(QString::fromStdString(executable), arguments);

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
        if (process.state() == QProcess::NotRunning) result.exitCode = process.exitCode();
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
    std::string program_;
    std::string defaultEntrypoint_;
    std::string defaultWorkingDirectory_;
    int defaultTimeoutMs_ = 0;
    int startTimeoutMs_ = 5000;
    int terminateGraceMs_ = 1000;
    std::atomic_bool running_{false};
    std::atomic_bool cancelRequested_{false};
};

ExecutionRequest requestFromNode(const ScenarioNode& node)
{
    ExecutionRequest request;
    request.target = stringValue(node.arguments, "target");
    request.workingDirectory = stringValue(node.arguments, "working_directory");
    request.timeoutMs = integerValue(node.arguments, "timeout_ms", 0);
    if (request.timeoutMs < 0) {
        throw std::invalid_argument("timeout_ms must be zero or positive");
    }

    std::vector<std::pair<unsigned long, std::string>> indexed;
    for (const auto& [key, value] : node.arguments) {
        if (key.rfind("arg.", 0) == 0) {
            const std::string suffix = key.substr(4);
            if (suffix.empty()
                || !std::all_of(suffix.begin(), suffix.end(),
                    [](unsigned char character) { return std::isdigit(character) != 0; })) {
                throw std::invalid_argument("Execution argument key must be arg.<N>: " + key);
            }
            indexed.emplace_back(std::stoul(suffix), value);
        } else if (key.rfind("env.", 0) == 0) {
            const std::string name = key.substr(4);
            if (name.empty()) {
                throw std::invalid_argument("Execution environment name is empty");
            }
            request.environment[name] = value;
        }
    }
    std::sort(indexed.begin(), indexed.end(),
        [](const auto& left, const auto& right) { return left.first < right.first; });
    for (std::size_t index = 1; index < indexed.size(); ++index) {
        if (indexed[index - 1].first == indexed[index].first) {
            throw std::invalid_argument("Duplicate execution argument index");
        }
    }
    request.arguments.reserve(indexed.size());
    for (auto& [index, value] : indexed) {
        (void)index;
        request.arguments.push_back(std::move(value));
    }
    return request;
}

std::string boundedEvidence(const std::string& value, bool& truncated)
{
    if (value.size() <= kEvidenceLimitBytes) return value;
    truncated = true;
    return value.substr(0, kEvidenceLimitBytes);
}

void emitExecutionEvent(
    ProcedureContext& context,
    const ScenarioNode& node,
    const std::string& message,
    RunVerdict verdict,
    std::map<std::string, std::string> data)
{
    if (!context.eventSink) return;
    context.eventSink({
        std::chrono::system_clock::now(), node.id, "EXECUTION", message, verdict,
        std::move(data)});
}

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

void registerExecutionProcedure(ScenarioEngine& engine, IExecutionRuntime& runtime)
{
    engine.registerProcedure("station.execute",
        [&runtime](const ScenarioNode& node, ProcedureContext& context) -> ProcedureResult {
            if (context.stopRequested.load()) {
                return {RunVerdict::Aborted, "Execution cancelled before start", {}};
            }

            const auto request = requestFromNode(node);
            const int expectedExitCode = integerValue(
                node.arguments, "expected_exit_code", 0);
            const std::string nonzeroVerdict = stringValue(
                node.arguments, "nonzero_verdict").empty()
                ? "fail" : stringValue(node.arguments, "nonzero_verdict");
            if (nonzeroVerdict != "fail" && nonzeroVerdict != "error") {
                throw std::invalid_argument(
                    "nonzero_verdict must be either fail or error");
            }

            emitExecutionEvent(context, node, "External execution started",
                RunVerdict::NotRun,
                {{"target", request.target},
                 {"timeout_ms", std::to_string(request.timeoutMs)}});

            std::atomic_bool finished{false};
            std::thread cancellationWatcher([&] {
                while (!finished.load()) {
                    if (context.stopRequested.load()) {
                        runtime.cancel();
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            });

            ExecutionResult execution;
            try {
                execution = runtime.execute(request);
                finished = true;
                cancellationWatcher.join();
            } catch (...) {
                finished = true;
                cancellationWatcher.join();
                throw;
            }

            RunVerdict verdict = RunVerdict::Ok;
            std::string message;
            if (!execution.started) {
                verdict = RunVerdict::Error;
                message = "External process did not start";
            } else if (execution.cancelled || context.stopRequested.load()) {
                verdict = RunVerdict::Aborted;
                message = "External process cancelled";
            } else if (execution.timedOut) {
                verdict = RunVerdict::Error;
                message = "External process timed out";
            } else if (execution.exitCode != expectedExitCode) {
                verdict = nonzeroVerdict == "error" ? RunVerdict::Error : RunVerdict::Fail;
                message = "External process returned unexpected exit code "
                    + std::to_string(execution.exitCode)
                    + " (expected " + std::to_string(expectedExitCode) + ")";
            } else {
                message = "External process completed with exit code "
                    + std::to_string(execution.exitCode);
            }

            bool stdoutTruncated = false;
            bool stderrTruncated = false;
            emitExecutionEvent(context, node, message, verdict,
                {{"target", request.target},
                 {"exit_code", std::to_string(execution.exitCode)},
                 {"expected_exit_code", std::to_string(expectedExitCode)},
                 {"started", execution.started ? "true" : "false"},
                 {"cancelled", execution.cancelled ? "true" : "false"},
                 {"timed_out", execution.timedOut ? "true" : "false"},
                 {"stdout", boundedEvidence(execution.standardOutput, stdoutTruncated)},
                 {"stderr", boundedEvidence(execution.standardError, stderrTruncated)},
                 {"stdout_truncated", stdoutTruncated ? "true" : "false"},
                 {"stderr_truncated", stderrTruncated ? "true" : "false"}});

            return {verdict, std::move(message), {}};
        });
}

} // namespace orbita::stand
