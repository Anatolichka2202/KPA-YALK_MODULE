#include "ktma/ubsi/equipment_readiness.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ktma::ubsi;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

orbita::stand::ComponentProfile equipment(
    std::string id,
    std::vector<std::string> capabilities,
    bool enabled = true)
{
    orbita::stand::ComponentProfile component;
    component.id = std::move(id);
    component.kind = "equipment";
    component.provider = "test.provider";
    component.enabled = enabled;
    component.capabilities = std::move(capabilities);
    return component;
}

} // namespace

int main()
{
    try {
        orbita::stand::StandProfile profile;
        profile.id = "ktma-readiness-test";
        profile.components = {
            equipment("ubsi-adapter", {"ulk.parameter_source"}),
            equipment("v7-reference", {"measure.reference_voltage"}),
            equipment("dc-supply", {"power.dc_supply"}),
            equipment("isd", {"stand.switch_matrix"}),
            equipment("unrelated-generator", {"signal.generator"}),
            equipment("disabled-waveform", {"measure.waveform"}, false),
        };

        const auto plan = buildEquipmentReadinessPlan(profile, {
            "power.dc_supply",
            "ulk.parameter_source",
            "stand.switch_matrix",
            "measure.reference_voltage",
        });
        require(plan.items.size() == 4, "readiness plan selected wrong equipment count");
        require(plan.items[0].componentId == "dc-supply"
                && plan.items[0].stage == EquipmentReadinessStage::Power,
            "power supply must be the first readiness stage");
        require(plan.items[1].componentId == "v7-reference"
                && plan.items[1].stage == EquipmentReadinessStage::Independent,
            "independent equipment order must follow delivery profile order");
        require(plan.items[2].componentId == "isd"
                && plan.items[2].stage == EquipmentReadinessStage::Independent,
            "switch matrix must be checked before adapter boot probe");
        require(plan.items[3].componentId == "ubsi-adapter"
                && plan.items[3].stage == EquipmentReadinessStage::Adapter,
            "ULK adapter must be the final readiness stage");
        require(plan.adapterBootDelayMilliseconds == 3000,
            "KTMA adapter boot delay changed unexpectedly");

        std::vector<std::string> trace;
        unsigned waited = 0;
        executeEquipmentReadinessPlan(profile, plan, {
            [&](const orbita::stand::ComponentProfile& component, bool armSupply) {
                trace.push_back(component.id + (armSupply ? ":arm" : ":probe"));
            },
            [&](unsigned milliseconds) {
                waited = milliseconds;
                trace.push_back("wait");
            },
        });

        const std::vector<std::string> expected = {
            "dc-supply:arm",
            "v7-reference:probe",
            "isd:probe",
            "wait",
            "ubsi-adapter:probe",
        };
        require(trace == expected, "KTMA power/wait/adapter sequence changed");
        require(waited == 3000, "KTMA adapter wait duration changed");

        const auto adapterOnly = buildEquipmentReadinessPlan(
            profile, {"ulk.parameter_source"});
        require(adapterOnly.items.size() == 2,
            "adapter readiness must include its DUT power dependency");
        require(adapterOnly.items[0].componentId == "dc-supply"
                && adapterOnly.items[0].stage == EquipmentReadinessStage::Power
                && adapterOnly.items[1].componentId == "ubsi-adapter"
                && adapterOnly.items[1].stage == EquipmentReadinessStage::Adapter,
            "adapter-only readiness must arm DUT power before probing the adapter");

        const auto all = buildEquipmentReadinessPlan(profile);
        require(all.items.size() == 6,
            "empty readiness selection must preserve all delivery equipment, including disabled entries");
        require(all.items.front().componentId == "dc-supply",
            "all-equipment plan must still start from DUT power");
        require(all.items.back().componentId == "ubsi-adapter",
            "all-equipment plan must still probe adapter last");

        auto scenarioProfile = profile;
        scenarioProfile.components[0].bindings = {"dut.parameter_source"};
        scenarioProfile.components[2].bindings = {"power.dut"};
        auto otherSupply = equipment("other-supply", {"power.dc_supply"});
        otherSupply.bindings = {"power.other"};
        scenarioProfile.components.push_back(otherSupply);
        orbita::stand::ScenarioDefinition scenario;
        orbita::stand::ScenarioNode node;
        node.requiredCapabilities.insert("ulk.parameter_source");
        node.requiredResources.push_back({"dut.parameter_source", "ulk.parameter_source"});
        scenario.steps.push_back(node);
        const auto scenarioPlan = buildScenarioEquipmentReadinessPlan(scenarioProfile, scenario);
        require(scenarioPlan.items.size() == 2 && scenarioPlan.items[0].componentId == "dc-supply"
                    && scenarioPlan.items[1].componentId == "ubsi-adapter",
                "scenario readiness armed an unrelated power supply or lost the DUT dependency");
        auto disabledSupply = equipment("disabled-dut-supply", {"power.dc_supply"}, false);
        disabledSupply.bindings = {"power.dut"};
        scenarioProfile.components.push_back(disabledSupply);
        require(buildScenarioEquipmentReadinessPlan(scenarioProfile, scenario).items.size() == 2,
                "disabled DUT supply made the enabled role ambiguous");
        scenarioProfile.components.pop_back();
        scenario.steps[0].requiredResources.push_back({"power.other", "power.dc_supply"});
        const auto separateSupplyPlan = buildScenarioEquipmentReadinessPlan(scenarioProfile, scenario);
        require(separateSupplyPlan.items.size() == 3
                    && separateSupplyPlan.items[0].componentId == "dc-supply"
                    && separateSupplyPlan.items[0].stage == EquipmentReadinessStage::Power
                    && separateSupplyPlan.items[1].componentId == "other-supply"
                    && separateSupplyPlan.items[1].stage == EquipmentReadinessStage::Independent,
                "non-DUT power supply was armed with the DUT voltage");
        scenarioProfile.components[2].bindings.clear();
        bool rejectedAmbiguousPower = false;
        try { (void)buildScenarioEquipmentReadinessPlan(scenarioProfile, scenario); }
        catch (const std::invalid_argument&) { rejectedAmbiguousPower = true; }
        require(rejectedAmbiguousPower, "ambiguous adapter power dependency was guessed");
        scenarioProfile.components.erase(scenarioProfile.components.begin() + 2);
        bool rejectedUnidentifiedPower = false;
        try { (void)buildScenarioEquipmentReadinessPlan(scenarioProfile, scenario); }
        catch (const std::invalid_argument&) { rejectedUnidentifiedPower = true; }
        require(rejectedUnidentifiedPower, "sole non-DUT supply was assumed to power the adapter");
        scenario.steps[0].requiredCapabilities.clear();
        scenario.steps[0].requiredResources = {{"power.other", "power.dc_supply"}};
        const auto otherOnly = buildScenarioEquipmentReadinessPlan(scenarioProfile, scenario);
        require(otherOnly.items.size() == 1 && otherOnly.items[0].componentId == "other-supply"
                    && otherOnly.items[0].stage == EquipmentReadinessStage::Independent,
                "sole non-DUT supply was armed without an adapter");
        auto multifunction = equipment("multifunction", {"ulk.parameter_source", "measure.reference_voltage"});
        multifunction.bindings = {"measure.multi"};
        scenarioProfile.components.push_back(multifunction);
        scenario.steps[0].requiredResources = {{"measure.multi", "measure.reference_voltage"}};
        const auto measurementOnly = buildScenarioEquipmentReadinessPlan(scenarioProfile, scenario);
        require(measurementOnly.items.size() == 1 && measurementOnly.items[0].componentId == "multifunction"
                    && measurementOnly.items[0].stage == EquipmentReadinessStage::Independent,
                "unrequested adapter capability triggered DUT power or adapter wait");
        scenario.steps.clear();
        require(buildScenarioEquipmentReadinessPlan(scenarioProfile, scenario).items.empty(),
                "empty scenario opened delivery equipment");

        std::cout << "KTMA equipment readiness ordering OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
