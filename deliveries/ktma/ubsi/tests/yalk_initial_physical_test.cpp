#include "ktma/ubsi/procedures.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace orbita::stand;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class FakeEquipment final : public ICapabilityProvider {
public:
    bool hasCapability(const std::string& capability) const override
    {
        return capability == "stand.switch_matrix"
            || capability == "ulk.parameter_source";
    }

    std::string invoke(
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments) override
    {
        if (capability == "stand.switch_matrix") {
            if (operation == "analog") {
                actions.push_back("analog:" + arguments.at("channel") + ":"
                    + arguments.at("enabled"));
                return "status=ok\n";
            }
            if (operation == "switch") {
                require(arguments.at("type") == "3",
                    "YALK initial cleanup must use type=3 only for overload sources");
                actions.push_back("switch:" + arguments.at("route") + ":"
                    + arguments.at("enabled"));
                return "status=ok\n";
            }
            if (operation == "release_owner") {
                actions.push_back("release");
                return "status=ok\n";
            }
            if (operation == "full_reset") {
                actions.push_back("full_reset");
                return "status=ok\n";
            }
            throw std::runtime_error("Unexpected switch operation: " + operation);
        }

        if (capability == "ulk.parameter_source" && operation == "stats")
            return "status=ready\nlast_sequence=" + std::to_string(sequence) + "\n";

        if (capability == "ulk.parameter_source" && operation == "read_snapshot") {
            afterSequences.push_back(static_cast<unsigned>(
                std::stoul(arguments.at("after_sequence"))));
            std::string words;
            for (unsigned index = 0; index < 100; ++index) {
                if (index) words += ',';
                // analog code 500 is below the seeded calibration zero 600.
                // Contact bit deliberately remains 0: for TU 1.1.4.10 it is
                // diagnostic only and must not turn a valid U<0 result into FAIL.
                words += "500";
            }
            ++sequence;
            return "status=ready\nsequence=" + std::to_string(sequence)
                + "\nwords=" + words + "\n";
        }
        throw std::runtime_error("Unexpected equipment operation");
    }

    void safeStopAll() noexcept override { safeStopped = true; }

    std::vector<std::string> actions;
    std::vector<unsigned> afterSequences;
    unsigned sequence = 40;
    bool safeStopped = false;
};

} // namespace

int main()
{
    try {
        ScenarioEngine engine;
        registerUbsiProcedures(engine);
        engine.registerProcedure("test.seed_yalk_calibration",
            [](const ScenarioNode&, ProcedureContext& context) {
                context.state["yalk.zero_code"] = "600";
                context.state["yalk.full_code"] = "1000";
                context.state["yalk.full_voltage"] = "6.2";
                return ProcedureResult{RunVerdict::Ok, "seeded", {}};
            });

        ScenarioDefinition scenario;
        scenario.id = "yalk-initial-physical";
        scenario.title = "YALK open-input physical contract";
        scenario.version = "1";
        scenario.catalogVersion = "test";
        scenario.objectType = "UBSI_468157_002";
        scenario.publicationState = PublicationState::Published;

        ScenarioNode seed;
        seed.id = "seed";
        seed.title = "Seed calibration";
        seed.tuRequirement = "test.fixture";
        seed.procedure = "test.seed_yalk_calibration";

        ScenarioNode initial;
        initial.id = "initial";
        initial.title = "Обрыв ЯЛК";
        initial.tuRequirement = "1.1.4.10";
        initial.procedure = "yalk.check_initial_state";
        initial.requiredCapabilities = {"ulk.parameter_source", "stand.switch_matrix"};
        initial.arguments = {
            {"channel_count", "80"},
            {"sample_count", "1"},
            {"preclean_settle_ms", "0"},
            {"full_scale_v", "6.2"},
        };
        scenario.steps = {seed, initial};

        FakeEquipment equipment;
        const auto run = engine.run(scenario, equipment, "test", "", false);

        require(run.verdict == RunVerdict::Ok,
            "Negative YALK voltage must pass even when the contact bit is zero");
        require(run.steps.size() == 2 && run.steps.back().measurements.size() == 80,
            "Open-input step must produce exactly one acceptance measurement per safe YALK address");
        require(equipment.afterSequences == std::vector<unsigned>({40}),
            "Open-input measurement must read a fresh snapshot after current last_sequence");
        require(equipment.safeStopped,
            "Scenario lifecycle must still execute global best-effort device safe-stop");
        require(std::find(equipment.actions.begin(), equipment.actions.end(), "full_reset")
                    == equipment.actions.end(),
            "YALK initial check must not use firmware type=4/global reset");

        for (const unsigned excluded : {29u, 30u, 31u, 44u, 71u, 72u, 73u, 88u}) {
            const std::string action = "analog:" + std::to_string(excluded) + ":false";
            require(std::find(equipment.actions.begin(), equipment.actions.end(), action)
                        == equipment.actions.end(),
                "YVP-owned ISD line was touched by YALK initial cleanup");
        }
        require(std::find(equipment.actions.begin(), equipment.actions.end(), "analog:87:false")
                    != equipment.actions.end(),
            "Targeted cleanup did not reach the last safe YALK address");
        require(std::find(equipment.actions.begin(), equipment.actions.end(),
                    "switch:yalk_overload_positive:false") != equipment.actions.end()
                && std::find(equipment.actions.begin(), equipment.actions.end(),
                    "switch:yalk_overload_negative:false") != equipment.actions.end(),
            "Targeted YALK cleanup must explicitly remove both common overload sources");

        const auto& first = run.steps.back().measurements.front();
        require(first.measured < 0.0 && first.attributes.at("signal") == "0"
                && first.attributes.at("signal_check") == "diagnostic_only",
            "Open-input evidence must retain negative voltage and diagnostic contact bit");

        std::cout << "YALK targeted open-input contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "YALK targeted open-input contract failed: " << error.what() << '\n';
        return 1;
    }
}
