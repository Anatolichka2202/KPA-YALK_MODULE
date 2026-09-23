#include "ktma/ubsi/procedures.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
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
            if (operation == "release_owner") {
                actions.push_back("release");
                return "status=ok\n";
            }
            if (operation == "analog") {
                const auto channel = arguments.at("channel");
                const auto enabled = arguments.at("enabled");
                actions.push_back("analog:" + channel + ":" + enabled);
                if (enabled == "true") ++analogEnableCount;
                return "status=ok\n";
            }
            if (operation == "switch") {
                require(arguments.count("type") && arguments.at("type") == "3",
                    "Overload switching must use ISD type=3");
                const auto enabled = arguments.at("enabled");
                if (arguments.count("route"))
                    actions.push_back("switch:route:" + arguments.at("route") + ":" + enabled);
                else
                    actions.push_back("switch:channel:" + arguments.at("channel") + ":" + enabled);
                return "status=ok\n";
            }
            throw std::runtime_error("Unexpected switch-matrix operation: " + operation);
        }

        if (capability == "ulk.parameter_source" && operation == "stats") {
            return "status=ready\nlast_sequence=" + std::to_string(sequence) + "\n";
        }
        if (capability == "ulk.parameter_source" && operation == "read_snapshot") {
            const unsigned after = static_cast<unsigned>(std::stoul(arguments.at("after_sequence")));
            afterSequences.push_back(after);
            std::string words;
            for (unsigned index = 0; index < 100; ++index) {
                if (index) words += ',';
                words += std::to_string(500u + index);
            }
            ++sequence;
            return "status=ready\nsequence=" + std::to_string(sequence)
                + "\nwords=" + words + "\n";
        }
        throw std::runtime_error("Unexpected equipment operation");
    }

    void safeStopAll() noexcept override { stopped = true; }

    std::vector<std::string> actions;
    std::vector<unsigned> afterSequences;
    unsigned sequence = 20;
    unsigned analogEnableCount = 0;
    bool stopped = false;
};

std::size_t findAfter(
    const std::vector<std::string>& actions,
    const std::string& value,
    std::size_t after)
{
    const auto found = std::find(actions.begin() + static_cast<std::ptrdiff_t>(after),
                                 actions.end(), value);
    if (found == actions.end()) throw std::runtime_error("Action not found: " + value);
    return static_cast<std::size_t>(found - actions.begin());
}

ScenarioDefinition scenarioWith(ScenarioNode overload)
{
    ScenarioDefinition scenario;
    scenario.id = "yalk-overload-physical";
    scenario.title = "YALK overload physical choreography";
    scenario.version = "1";
    scenario.catalogVersion = "1";
    scenario.objectType = "UBSI_468157_002";
    scenario.publicationState = PublicationState::Published;
    scenario.steps = {std::move(overload)};
    return scenario;
}

ScenarioNode baseOverloadNode()
{
    ScenarioNode overload;
    overload.id = "overload";
    overload.title = "Перегрузка";
    overload.tuRequirement = "1.1.4.11";
    overload.procedure = "yalk.check_overload";
    overload.requiredCapabilities = {"ulk.parameter_source", "stand.switch_matrix"};
    overload.arguments = {
        {"mapping_confirmed", "true"},
        {"sample_count", "1"},
        {"baseline_settle_ms", "0"},
        {"dac_off_settle_ms", "0"},
        {"overload_settle_ms", "0"},
        {"cleanup_settle_ms", "0"},
        {"maximum_code_delta", "2"},
    };
    return overload;
}

} // namespace

int main()
{
    try {
        ScenarioEngine engine;
        registerUbsiProcedures(engine);

        auto overload = baseOverloadNode();
        overload.arguments["physical_channels"] = "1-2";
        overload.arguments["stressed_channels"] = "1-2";
        overload.arguments["observed_addresses"] = "1-2";
        const auto scenario = scenarioWith(std::move(overload));

        FakeEquipment equipment;
        const auto run = engine.run(scenario, equipment, "test", "", false);

        require(run.verdict == RunVerdict::Ok, "Physical overload regression must pass");
        require(run.steps.size() == 1 && run.steps.front().measurements.size() == 4,
            "Two targets and two polarities must produce four other-channel comparisons");
        require(equipment.afterSequences == std::vector<unsigned>({20, 21, 22, 23, 24}),
            "Every overload sample must be anchored after the latest live sequence");
        require(equipment.analogEnableCount == 6,
            "Background must be created once and only the stressed DAC restored after each impact");
        require(equipment.stopped, "Scenario lifecycle must still call safeStopAll");
        require(std::find(equipment.actions.begin(), equipment.actions.end(), "full_reset")
                    == equipment.actions.end(),
            "Physical overload must never use firmware type=4/global reset");

        // First +12 V impact on target 1: target DAC off -> common source on ->
        // target switch on -> target switch off -> common source off -> DAC restore.
        const auto dacOff = findAfter(equipment.actions, "analog:1:false", 0);
        const auto sourceOn = findAfter(
            equipment.actions, "switch:route:yalk_overload_positive:true", dacOff + 1);
        const auto targetOn = findAfter(
            equipment.actions, "switch:channel:1:true", sourceOn + 1);
        const auto targetOff = findAfter(
            equipment.actions, "switch:channel:1:false", targetOn + 1);
        const auto sourceOff = findAfter(
            equipment.actions, "switch:route:yalk_overload_positive:false", targetOff + 1);
        const auto restore = findAfter(equipment.actions, "analog:1:true", sourceOff + 1);
        require(dacOff < sourceOn && sourceOn < targetOn && targetOn < targetOff
                && targetOff < sourceOff && sourceOff < restore,
            "Overload command order differs from the confirmed donor choreography");

        const auto& first = run.steps.front().measurements.front();
        require(first.attributes.at("stressed_channel") == "1"
                && first.attributes.at("observed_channel") == "2"
                && first.attributes.at("lower_delta_code") == "-2"
                && first.attributes.at("upper_delta_code") == "2",
            "Per-channel overload evidence must retain target, observation and current +/-2 criterion");

        // The currently published master scenario still carries historical
        // *_count=88 arguments. Until that data file is migrated to explicit
        // lists, the runtime must fail-safe to the confirmed 80-address YALK map
        // and never drive the eight YVP-owned ISD lines.
        auto legacyNode = baseOverloadNode();
        legacyNode.arguments["physical_channel_count"] = "88";
        legacyNode.arguments["observed_address_count"] = "88";
        legacyNode.arguments["stressed_channels"] = "1";
        FakeEquipment legacyEquipment;
        const auto legacyRun = engine.run(
            scenarioWith(std::move(legacyNode)), legacyEquipment, "test", "", false);
        require(legacyRun.verdict == RunVerdict::Ok,
            "Legacy 88-count scenario must execute through the safe-map compatibility gate");
        require(legacyRun.steps.front().measurements.size() == 158,
            "One stressed channel and two polarities must observe the other 79 safe YALK addresses");

        for (const unsigned excluded : {29u, 30u, 31u, 44u, 71u, 72u, 73u, 88u}) {
            const std::string analogOn = "analog:" + std::to_string(excluded) + ":true";
            const std::string targetOnAction = "switch:channel:" + std::to_string(excluded) + ":true";
            require(std::find(legacyEquipment.actions.begin(), legacyEquipment.actions.end(), analogOn)
                        == legacyEquipment.actions.end(),
                "YVP-owned ISD line was incorrectly enabled as YALK background");
            require(std::find(legacyEquipment.actions.begin(), legacyEquipment.actions.end(), targetOnAction)
                        == legacyEquipment.actions.end(),
                "YVP-owned ISD line was incorrectly selected as YALK overload target");
        }
        require(std::find(legacyEquipment.actions.begin(), legacyEquipment.actions.end(), "analog:87:true")
                    != legacyEquipment.actions.end(),
            "Safe-map compatibility gate did not reach the final confirmed YALK address");

        std::cout << "YALK physical overload choreography OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "YALK physical overload choreography failed: " << error.what() << '\n';
        return 1;
    }
}
