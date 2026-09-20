#include "backend/scenario_engine.h"
#include "backend/scenario_yaml.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

tu::ProcedureResult ok(const tu::ScenarioStep&, tu::ProcedureContext&)
{
    return {tu::RunVerdict::Ok, "OK", {}};
}

const tu::ScenarioStep& stepById(const tu::ScenarioDefinition& scenario,
                                 const std::string& id)
{
    for (const auto& step : scenario.steps) if (step.id == id) return step;
    throw std::runtime_error("missing scenario step: " + id);
}

std::string argument(const tu::ScenarioStep& step, const std::string& key)
{
    const auto found = step.arguments.find(key);
    if (found == step.arguments.end())
        throw std::runtime_error("missing argument " + key + " for " + step.id);
    return found->second;
}

} // namespace

int main()
{
    try {
        auto scenario = tu::loadScenarioYaml(TU_SOURCE_DIR "/data/ubsi_tu.yaml");
        require(scenario.id == "ubsi.tu.normal", "wrong scenario id");
        require(scenario.version == "1.1.3", "unexpected TU scenario version");

        const std::vector<std::string> expectedSteps{
            "isd_baseline",
            "readiness",
            "supply_range",
            "yalk_stream",
            "yalk_calibration",
            "yalk_initial",
            "yalk_channels",
            "yalk_contacts",
            "yalk_overload",
            "yalk_reference_voltage",
            "yalk_cleanup",
            "ytp_stream",
            "ytp_calibration",
            "ytp_channels",
            "ytp_cleanup",
            "yvp_channels",
            "power_off",
        };
        require(scenario.steps.size() == expectedSteps.size(), "TU route step count changed");
        for (std::size_t index = 0; index < expectedSteps.size(); ++index)
            require(scenario.steps[index].id == expectedSteps[index], "TU route order changed");

        const auto& yalk = stepById(scenario, "yalk_channels");
        require(yalk.procedure == "yalk.channels", "YALK analog procedure changed");
        require(argument(yalk, "addresses") == "1-28,32-43,45-70,74-87",
                "YALK verified address map changed");
        require(argument(yalk, "point_volts") == "0,3.1,6.2",
                "YALK analog points changed");

        const auto& contacts = stepById(scenario, "yalk_contacts");
        require(argument(contacts, "contact_points_v") == "0,0.9,2.5",
                "YALK contact points changed");
        require(argument(contacts, "signal_expectations") == "0,0,1",
                "YALK contact truth table changed");

        const auto& overload = stepById(scenario, "yalk_overload");
        require(argument(overload, "physical_channels") == "1-88",
                "YALK overload physical map changed");
        require(argument(overload, "observed_addresses") == "1-88",
                "YALK overload observed map changed");
        require(argument(overload, "positive_overload_contact") == "96",
                "YALK +12 route changed");
        require(argument(overload, "negative_overload_contact") == "95",
                "YALK -12 route changed");
        require(argument(overload, "overload_settle_ms") == "10000",
                "YALK overload hold changed");
        require(argument(overload, "maximum_code_delta") == "2",
                "YALK overload delta criterion changed");

        const auto& reference = stepById(scenario, "yalk_reference_voltage");
        require(argument(reference, "nominal_v") == "6.2", "YALK reference nominal changed");
        require(argument(reference, "tolerance_v") == "0.03", "YALK reference tolerance changed");

        const auto& ytp = stepById(scenario, "ytp_channels");
        require(argument(ytp, "resistance_points_ohm") == "0,120,240",
                "YTP R4831 points changed");
        require(argument(ytp, "full_scale_ohm") == "240", "YTP scale changed");
        require(argument(ytp, "tolerance_percent_fs") == "0.5", "YTP tolerance changed");

        const auto& yvp = stepById(scenario, "yvp_channels");
        require(argument(yvp, "gains_mv_per_pcl") == "0.25,0.5,1,2,4,8,32",
                "YVP gain matrix changed");
        require(argument(yvp, "frequencies_hz") == "2,6,20,500,1800,2000,4000",
                "YVP frequency matrix changed");
        require(argument(yvp, "coupling_capacitance_pf") == "1000",
                "YVP coupling capacitor changed");
        require(argument(yvp, "reference_frequency_hz") == "500",
                "YVP reference frequency changed");
        require(argument(yvp, "gain_tolerance_percent") == "7",
                "YVP gain tolerance changed");
        require(argument(yvp, "attenuation_min_db") == "20.0",
                "YVP attenuation criterion changed");

        const std::vector<std::string> expectedInputs{"33","34","35","36","37","38","39","40"};
        const std::vector<std::string> expectedMeasurements{"44","29","30","31","71","72","88","73"};
        for (std::size_t index = 0; index < 8; ++index) {
            const std::string number = std::to_string(index + 1);
            require(argument(yvp, "input_" + number + "_contacts") == expectedInputs[index],
                    "YVP input channel map changed");
            require(argument(yvp, "measurement_" + number + "_contacts") == expectedMeasurements[index],
                    "YVP measurement channel map changed");
        }

        require(argument(yvp, "gain_0_25_bits") == "none", "YVP K0.25 map changed");
        require(argument(yvp, "gain_0_5_bits") == "1", "YVP K0.5 map changed");
        require(argument(yvp, "gain_1_bits") == "2", "YVP K1 map changed");
        require(argument(yvp, "gain_2_bits") == "3", "YVP K2 map changed");
        require(argument(yvp, "gain_4_bits") == "1,3", "YVP K4 map changed");
        require(argument(yvp, "gain_8_bits") == "4", "YVP K8 map changed");
        require(argument(yvp, "gain_32_bits") == "2,4", "YVP K32 map changed");

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
