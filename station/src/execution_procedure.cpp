#include "orbita_stand/execution_procedure.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace orbita::stand {
namespace {

constexpr std::size_t kEvidenceLimitBytes = 64 * 1024;

int integerArgument(
    const std::map<std::string, std::string>& arguments,
    const std::string& key,
    int fallback)
{
    const auto found = arguments.find(key);
    if (found == arguments.end() || found->second.empty()) return fallback;
    std::size_t parsed = 0;
    const int value = std::stoi(found->second, &parsed);
    if (parsed != found->second.size()) {
        throw std::invalid_argument("Invalid integer scenario argument: " + key);
    }
    return value;
}

std::string boundedEvidence(const std::string& value, bool& truncated)
{
    if (value.size() <= kEvidenceLimitBytes) return value;
    truncated = true;
    return value.substr(0, kEvidenceLimitBytes);
}

std::vector<std::string> orderedArguments(
    const std::map<std::string, std::string>& arguments)
{
    std::vector<std::pair<unsigned long, std::string>> indexed;
    for (const auto& [key, value] : arguments) {
        if (key.rfind("arg.", 0) != 0) continue;
        const std::string suffix = key.substr(4);
        if (suffix.empty()
            || !std::all_of(suffix.begin(), suffix.end(),
                [](unsigned char value) { return std::isdigit(value) != 0; })) {
            throw std::invalid_argument("Execution argument key must be arg.<N>: " + key);
        }
        indexed.emplace_back(std::stoul(suffix), value);
    }
    std::sort(indexed.begin(), indexed.end(),
        [](const auto& left, const auto& right) { return left.first < right.first; });
    for (std::size_t index = 1; index < indexed.size(); ++index) {
        if (indexed[index - 1].first == indexed[index].first) {
            throw std::invalid_argument("Duplicate execution argument index");
        }
    }
    std::vector<std::string> result;
    result.reserve(indexed.size());
    for (auto& [index, value] : indexed) {
        (void)index;
        result.push_back(std::move(value));
    }
    return result;
}

ExecutionRequest buildRequest(const ScenarioNode& node)
{
    ExecutionRequest request;
    if (const auto found = node.arguments.find("target"); found != node.arguments.end())
        request.target = found->second;
    if (const auto found = node.arguments.find("working_directory"); found != node.arguments.end())
        request.workingDirectory = found->second;
    request.timeoutMs = integerArgument(node.arguments, "timeout_ms", 0);
    if (request.timeoutMs < 0)
        throw std::invalid_argument("timeout_ms must be zero or positive");
    request.arguments = orderedArguments(node.arguments);
    for (const auto& [key, value] : node.arguments) {
        if (key.rfind("env.", 0) != 0) continue;
        const std::string name = key.substr(4);
        if (name.empty()) throw std::invalid_argument("Execution environment name is empty");
        request.environment[name] = value;
    }
    return request;
}

void emitExecutionEvent(
    ProcedureContext& context,
    const ScenarioNode& node,
    const std::string& message,
    RunVerdict verdict,
    std::map<std::string, std::string> data)
{
    if (!context.eventSink) return;
    context.eventSink(RunEvent{
        std::chrono::system_clock::now(), node.id, "EXECUTION", message, verdict,
        std::move(data)});
}

} // namespace

void registerExecutionProcedure(ScenarioEngine& engine, IExecutionRuntime& runtime)
{
    engine.registerProcedure("station.execute",
        [&runtime](const ScenarioNode& node, ProcedureContext& context) -> ProcedureResult {
            if (context.stopRequested.load()) {
                return {RunVerdict::Aborted, "Execution cancelled before start", {}};
            }

            const auto request = buildRequest(node);
            const int expectedExitCode = integerArgument(
                node.arguments, "expected_exit_code", 0);
            const auto verdictArgument = node.arguments.find("nonzero_verdict");
            const std::string nonzeroVerdict = verdictArgument == node.arguments.end()
                ? "fail" : verdictArgument->second;
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
            auto stdoutEvidence = boundedEvidence(execution.standardOutput, stdoutTruncated);
            auto stderrEvidence = boundedEvidence(execution.standardError, stderrTruncated);
            emitExecutionEvent(context, node, message, verdict,
                {{"target", request.target},
                 {"exit_code", std::to_string(execution.exitCode)},
                 {"expected_exit_code", std::to_string(expectedExitCode)},
                 {"started", execution.started ? "true" : "false"},
                 {"cancelled", execution.cancelled ? "true" : "false"},
                 {"timed_out", execution.timedOut ? "true" : "false"},
                 {"stdout", std::move(stdoutEvidence)},
                 {"stderr", std::move(stderrEvidence)},
                 {"stdout_truncated", stdoutTruncated ? "true" : "false"},
                 {"stderr_truncated", stderrTruncated ? "true" : "false"}});

            return {verdict, std::move(message), {}};
        });
}

} // namespace orbita::stand
