#include "orbita_stand/execution_procedure.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace orbita::stand;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class NoEquipment final : public ICapabilityProvider {
public:
    bool hasCapability(const std::string&) const override { return false; }
    std::string invoke(
        const std::string&,
        const std::string&,
        const std::map<std::string, std::string>&) override
    {
        throw std::runtime_error("Unexpected equipment invocation");
    }
    void safeStopAll() noexcept override { stopped = true; }
    bool stopped = false;
};

class FakeRuntime final : public IExecutionRuntime {
public:
    ExecutionResult execute(const ExecutionRequest& request) override
    {
        lastRequest = request;
        running = true;
        if (blockUntilCancelled) {
            while (!cancelled.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            running = false;
            auto result = next;
            result.started = true;
            result.cancelled = true;
            return result;
        }
        running = false;
        return next;
    }

    void cancel() noexcept override
    {
        cancelCalled = true;
        cancelled = true;
    }

    bool isRunning() const noexcept override { return running.load(); }

    ExecutionResult next;
    ExecutionRequest lastRequest;
    std::atomic_bool running{false};
    std::atomic_bool cancelled{false};
    bool cancelCalled = false;
    bool blockUntilCancelled = false;
};

ScenarioDefinition scenario(std::map<std::string, std::string> arguments)
{
    ScenarioDefinition result;
    result.id = "execution-test";
    result.title = "Execution test";
    result.version = "1";
    result.catalogVersion = "1";
    result.publicationState = PublicationState::Published;
    ScenarioNode step;
    step.id = "external";
    step.title = "External bench";
    // ScenarioEngine requires every leaf to retain an auditable requirement
    // reference, even when the procedure is generic rather than KTMA-specific.
    step.tuRequirement = "integration/external-bench";
    step.procedure = "station.execute";
    step.arguments = std::move(arguments);
    result.steps.push_back(std::move(step));
    return result;
}

void successContract()
{
    FakeRuntime runtime;
    runtime.next.started = true;
    runtime.next.exitCode = 7;
    runtime.next.standardOutput = "bench-ok\n";
    runtime.next.standardError = "diagnostic\n";

    ScenarioEngine engine;
    registerExecutionProcedure(engine, runtime);
    NoEquipment equipment;
    const auto run = engine.run(scenario({
        {"target", "bench.py"},
        {"arg.1", "second"},
        {"arg.0", "first"},
        {"env.BENCH_MODE", "acceptance"},
        {"working_directory", "work"},
        {"timeout_ms", "2500"},
        {"expected_exit_code", "7"},
    }), equipment, "profile-1", "SN-1", false);

    require(run.verdict == RunVerdict::Ok,
        "expected external exit code must produce OK");
    require(runtime.lastRequest.target == "bench.py",
        "execution target was not forwarded");
    require(runtime.lastRequest.arguments.size() == 2
            && runtime.lastRequest.arguments[0] == "first"
            && runtime.lastRequest.arguments[1] == "second",
        "indexed execution arguments were not ordered");
    require(runtime.lastRequest.environment.at("BENCH_MODE") == "acceptance",
        "execution environment was not forwarded");
    require(runtime.lastRequest.workingDirectory == "work"
            && runtime.lastRequest.timeoutMs == 2500,
        "execution working directory/timeout were not forwarded");
    require(run.events.size() >= 2,
        "execution procedure must persist start and finish events");
    const auto executionFinish = std::find_if(
        run.events.rbegin(), run.events.rend(), [](const RunEvent& event) {
            return event.stage == "EXECUTION";
        });
    require(executionFinish != run.events.rend()
            && executionFinish->verdict == RunVerdict::Ok,
        "execution finish event has wrong stage/verdict");
    require(executionFinish->data.at("stdout") == "bench-ok\n"
            && executionFinish->data.at("stderr") == "diagnostic\n"
            && executionFinish->data.at("exit_code") == "7",
        "execution evidence was not attached to the run event");
    require(equipment.stopped, "scenario engine did not safe-stop equipment");
}

void failureMappingContract()
{
    FakeRuntime runtime;
    runtime.next.started = true;
    runtime.next.exitCode = 3;

    ScenarioEngine engine;
    registerExecutionProcedure(engine, runtime);
    NoEquipment equipment;

    auto run = engine.run(scenario({{"expected_exit_code", "0"}}),
        equipment, "profile-1", "SN-1", false);
    require(run.verdict == RunVerdict::Fail,
        "unexpected completed exit code must default to FAIL");

    run = engine.run(scenario({
        {"expected_exit_code", "0"}, {"nonzero_verdict", "error"}}),
        equipment, "profile-1", "SN-1", false);
    require(run.verdict == RunVerdict::Error,
        "delivery must be able to classify nonzero exit as ERROR");

    runtime.next = {};
    runtime.next.standardError = "cannot spawn";
    run = engine.run(scenario({}), equipment, "profile-1", "SN-1", false);
    require(run.verdict == RunVerdict::Error,
        "process start failure must produce ERROR");

    runtime.next.started = true;
    runtime.next.timedOut = true;
    runtime.next.exitCode = -1;
    run = engine.run(scenario({}), equipment, "profile-1", "SN-1", false);
    require(run.verdict == RunVerdict::Error,
        "execution timeout must produce ERROR");
}

void cancellationContract()
{
    FakeRuntime runtime;
    runtime.blockUntilCancelled = true;

    ScenarioEngine engine;
    registerExecutionProcedure(engine, runtime);
    NoEquipment equipment;
    ScenarioRunResult run;

    std::thread worker([&] {
        run = engine.run(scenario({}), equipment, "profile-1", "SN-1", false);
    });
    for (int attempt = 0; attempt < 100 && !runtime.isRunning(); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    require(runtime.isRunning(), "blocking execution runtime did not start");
    engine.requestStop();
    worker.join();

    require(runtime.cancelCalled,
        "scenario stop request did not cancel execution runtime");
    require(run.verdict == RunVerdict::Aborted,
        "cancelled external execution must produce ABORTED");
}

} // namespace

int main()
{
    try {
        successContract();
        failureMappingContract();
        cancellationContract();
        std::cout << "execution scenario contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "execution scenario contract failed: " << error.what() << '\n';
        return 1;
    }
}
