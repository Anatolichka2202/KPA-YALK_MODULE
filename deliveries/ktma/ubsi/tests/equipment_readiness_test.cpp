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

        const auto all = buildEquipmentReadinessPlan(profile);
        require(all.items.size() == 6,
            "empty readiness selection must preserve all delivery equipment, including disabled entries");
        require(all.items.front().componentId == "dc-supply",
            "all-equipment plan must still start from DUT power");
        require(all.items.back().componentId == "ubsi-adapter",
            "all-equipment plan must still probe adapter last");

        std::cout << "KTMA equipment readiness ordering OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
