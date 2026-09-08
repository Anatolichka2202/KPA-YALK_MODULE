#include "orbita_stand/scenario.h"
#include "orbita_stand/ubsi_procedures.h"
#include "orbita_stand/ubsi_yvp_math.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef ORBITA_SOURCE_DIR
#define ORBITA_SOURCE_DIR "."
#endif

using namespace orbita::stand;

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

std::string readFile(const std::string& relative)
{
    const std::string path = std::string(ORBITA_SOURCE_DIR) + "/" + relative;
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open " + path);
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

bool contains(const std::string& text, const std::string& value)
{
    return text.find(value) != std::string::npos;
}

void yvpAddressContract()
{
    std::istringstream input(readFile("data/catalog/address_sets/ulk_yvp_reference.txt"));
    std::vector<unsigned> addresses;
    unsigned value = 0;
    while (input >> value) addresses.push_back(value);
    require(addresses.size() == 8, "YVP address set must contain exactly 8 addresses");
    for (unsigned index = 0; index < addresses.size(); ++index) {
        require(addresses[index] == 89 + index,
            "YVP ulk_address must be 89..96 (word_index 88..95)");
    }

    const auto catalog = readFile("data/catalog/catalog.yaml");
    const auto begin = catalog.find("parameter_group: yvp_fast", catalog.find("bindings:"));
    require(begin != std::string::npos, "catalog must contain yvp_fast binding");
    const auto end = catalog.find("\ninstances:", begin);
    const auto block = catalog.substr(begin, end - begin);
    require(contains(block, "source: ulk.parameter_source"),
        "YVP must use ulk.parameter_source");
    require(contains(block, "address_file: address_sets/ulk_yvp_reference.txt"),
        "YVP must use the dedicated YALK address set");
    require(contains(block, "count: 8"), "YVP binding count must be 8");
    require(!contains(block, "orbita.parameter_source"),
        "YVP current binding must not use Orbita/E20");
}

void scenarioContract()
{
    const auto combined = readFile("data/scenarios/ubsi_ulk_combined_check.yaml");
    require(!contains(combined, "1.1.4.6"), "1.1.4.6/50 m must not be in current TU scenario");
    require(!contains(combined, "ubsi.sensor_supply"),
        "350/450 mA sensor-supply procedure must not be in current TU scenario");
    require(!contains(combined, "ubsi.external_evidence"),
        "excluded checks must not return as external-evidence gates");
    require(contains(combined, "maximum_total_current_a: 0.4"),
        "whole-UBSI current criterion must be 0.4 A");
    require(contains(combined, "supply_current_limit_a: 0.6"),
        "hardware current limit must remain separate from the 0.4 A criterion");
    require(contains(combined, "frequencies_hz: 0.15,20,250,500,1800,2000,4000"),
        "YVP frequency set is wrong");
    require(contains(combined, "gains_mv_per_pcl: 0.25,0.5,1,2,4,8,32"),
        "YVP gain set is wrong");
    require(contains(combined, "routes_confirmed: false"),
        "uncommissioned YVP routing must be fail-safe");
    require(contains(combined, "yalk_value_model_confirmed: false"),
        "uncommissioned YVP Uout model must be fail-safe");

    const auto legacy = readFile("data/scenarios/ubsi_tu_5_6.yaml");
    require(!contains(legacy, "1.1.4.6"),
        "legacy trace scenario must not reintroduce the 50 m check");
    require(!contains(legacy, "procedure: ubsi.sensor_supply"),
        "legacy trace scenario must not reintroduce 350/450 mA automation");
    require(!contains(legacy, "orbita.parameter_source"),
        "legacy trace scenario must not reintroduce Orbita as YVP source");
}

void yvpMathContract()
{
    const double q = yvpChargePc(1000.0, 2.0);
    require(std::abs(q - 2000.0) < 1e-9, "Q = C*U calculation is wrong");
    const double gain = yvpGainMvPerPc(2.0, q);
    require(std::abs(gain - 1.0) < 1e-9, "Kmeas calculation is wrong");
    require(std::abs(yvpRelativeErrorPercent(gain, 1.0)) < 1e-9,
        "gain relative error calculation is wrong");
    require(std::abs(yvpAfcPercent(1.05, 1.0) - 5.0) < 1e-9,
        "AFC calculation is wrong");
    require(std::abs(yvpAttenuationDb(1.0, 0.1) - 20.0) < 1e-9,
        "attenuation calculation is wrong");
    require(yvpStimulusVppForGain(0.25) == 8.0, "0.25 mV/pC stimulus must be 8 V");
    require(yvpStimulusVppForGain(0.5) == 4.0, "0.5 mV/pC stimulus must be 4 V");
    require(yvpStimulusVppForGain(1.0) == 2.0, "1 mV/pC stimulus must be 2 V");
    require(yvpStimulusVppForGain(2.0) == 1.0, ">1 mV/pC stimulus must be 1 V");
}

} // namespace

int main()
{
    try {
        // Also forces the current wrapper and the renamed legacy registrar to
        // link together; duplicate/undefined registration symbols fail here.
        ScenarioEngine engine;
        registerUbsiProcedures(engine);
        yvpAddressContract();
        scenarioContract();
        yvpMathContract();
        std::cout << "UBSI backend contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
