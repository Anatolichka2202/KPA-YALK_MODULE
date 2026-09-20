#include "backend/scenario_engine.h"
#include "backend/scenario_yaml.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

tu::ProcedureResult ok(const tu::ScenarioStep&, tu::ProcedureContext&)
{
    return {tu::RunVerdict::Ok, "OK", {}};
}

} // namespace

int main()
{
    try {
        auto scenario = tu::loadScenarioYaml(TU_SOURCE_DIR "/data/ubsi_tu.yaml");
        require(scenario.id == "ubsi.tu.normal", "wrong scenario id");
        require(!scenario.steps.empty(), "empty TU scenario");

        tu::ScenarioEngine validationEngine;
        for (const auto& step : scenario.steps)
            validationEngine.registerProcedure(step.procedure, ok);
        require(validationEngine.validate(scenario).empty(), "TU scenario does not validate");

        bool duplicateRejected = false;
        try { validationEngine.registerProcedure(scenario.steps.front().procedure, ok); }
        catch (const std::logic_error&) { duplicateRejected = true; }
        require(duplicateRejected, "duplicate procedure registration must fail");

        tu::ScenarioDefinition skipScenario;
        skipScenario.id = "skip-test";
        skipScenario.title = "skip-test";
        skipScenario.version = "1";
        skipScenario.steps = {
            {"first", "first", "test", "wait", {}},
            {"second", "second", "test", "ok", {}},
        };

        tu::ScenarioEngine skipEngine;
        std::atomic_bool entered{false};
        skipEngine.registerProcedure("wait", [&](const tu::ScenarioStep&, tu::ProcedureContext& context) {
            entered.store(true);
            while (true) {
                context.checkpoint();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return tu::ProcedureResult{};
        });
        skipEngine.registerProcedure("ok", ok);

        tu::ScenarioRunResult skipped;
        std::thread runThread([&] { skipped = skipEngine.run(skipScenario, "SN-TEST"); });
        while (!entered.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        skipEngine.requestSkipCurrent();
        runThread.join();

        require(skipped.steps.size() == 2, "skip must continue with next step");
        require(skipped.steps[0].verdict == tu::RunVerdict::Ok, "skipped step must be NORMA/OK");
        require(skipped.steps[0].operatorSkipped, "skip audit flag missing");
        require(skipped.steps[1].verdict == tu::RunVerdict::Ok, "step after skip did not run");
        require(skipped.verdict == tu::RunVerdict::Ok, "skip must not degrade overall verdict");

        std::cout << "scenario_engine_test: OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scenario_engine_test: " << error.what() << '\n';
        return 1;
    }
}
