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

bool tuReferences(const std::string& yaml, const std::string& requirement)
{
    std::istringstream lines(yaml);
    std::string line;
    while (std::getline(lines, line)) {
        const auto tag = line.find("tu:");
        if (tag != std::string::npos && line.find(requirement, tag) != std::string::npos)
            return true;
    }
    return false;
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
    require(!tuReferences(combined, "1.1.4.6"),
        "1.1.4.6/50 m must not be a current TU step");
    require(!tuReferences(combined, "1.1.4.4"),
        "1.1.4.4 must not be a current TU step");
    require(!contains(combined, "ubsi.sensor_supply"),
        "350/450 mA sensor-supply procedure must not be in current TU scenario");
    require(!contains(combined, "ubsi.external_evidence"),
        "excluded checks must not return as external-evidence gates");
    require(!contains(combined, "orbita.parameter_source"),
        "YVP current TU route must not use Orbita/E20");
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
    require(!tuReferences(legacy, "1.1.4.6"),
        "legacy trace scenario must not reintroduce the 50 m check");
    require(!contains(legacy, "procedure: ubsi.sensor_supply"),
        "legacy trace scenario must not reintroduce 350/450 mA automation");
    require(!contains(legacy, "orbita.parameter_source"),
        "legacy trace scenario must not reintroduce Orbita as YVP source");

    const auto ytp = readFile("data/scenarios/ubsi_ytp_tu_5_6.yaml");
    require(!tuReferences(ytp, "1.1.4.6"),
        "standalone YTP scenario must not claim the 50 m requirement");

    const auto traceability = readFile("data/scenarios/ubsi_tu_5_6_traceability.csv");
    require(!contains(traceability, "1.1.4.2;5.6;ubsi.external_evidence"),
        "sensor-supply must not be represented as external evidence");
    require(!contains(traceability, "1.1.4.14;5.6;ubsi.external_evidence;входной ток"),
        "per-channel input current must not be represented as external evidence");
    require(contains(traceability, "1.1.4.6;5.6;нет;50-метровая линия"),
        "traceability must explicitly record 1.1.4.6 as not checked");
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

class ContractEquipment final : public ICapabilityProvider {
public:
    bool hasCapability(const std::string&) const override { return true; }

    std::string invoke(const std::string& capability, const std::string& operation,
                       const std::map<std::string, std::string>& arguments) override
    {
        operations.push_back(capability + ":" + operation);
        if (capability == "catalog.parameter_resolver" && operation == "resolve") {
            const unsigned channel = static_cast<unsigned>(std::stoul(arguments.at("channel_index")));
            const unsigned address = 89 + channel;
            return "source=ulk.parameter_source\nlocator_type=ulk_address\nlocator="
                + std::to_string(address)
                + "\nstream_id=\nword_index=" + std::to_string(address - 1)
                + "\nmask=1023\nshift=0\nmode=0\nconversion_id=yalk_two_point_6v2\n"
                  "stimulus_route=\nstimulus_offset=0\nconfirmed=true\n";
        }
        if (capability == "power.dc_supply" && operation == "set_voltage") {
            supplyVoltage = std::stod(arguments.at("volts"));
            return "status=ok\n";
        }
        if (capability == "power.dc_supply" && operation == "set_current_limit") {
            hardwareCurrentLimit = std::stod(arguments.at("amperes"));
            return "status=ok\n";
        }
        if (capability == "power.dc_supply" && operation == "output") {
            outputEnabled = arguments.at("enabled") == "true";
            return "status=ok\n";
        }
        if (capability == "power.dc_supply" && operation == "read_state") {
            const double current = std::abs(supplyVoltage - 35.0) < 0.01 ? 0.41 : 0.20;
            return "status=ready\nvolts=" + std::to_string(supplyVoltage)
                + "\namperes=" + std::to_string(current)
                + "\noutput_enabled=" + (outputEnabled ? std::string("true\n") : std::string("false\n"));
        }
        if (capability == "ulk.parameter_source" && operation == "alive")
            return "status=ready\n";
        return "status=ready\n";
    }

    void safeStopAll() noexcept override { stopped = true; }

    std::vector<std::string> operations;
    double supplyVoltage = 27.0;
    double hardwareCurrentLimit = 0.0;
    bool outputEnabled = false;
    bool stopped = false;
};

ScenarioDefinition oneStep(std::string procedure,
                           std::map<std::string, std::string> arguments = {})
{
    ScenarioDefinition scenario;
    scenario.id = "contract";
    scenario.title = "contract";
    scenario.version = "1";
    scenario.catalogVersion = "1";
    scenario.publicationState = PublicationState::Published;
    ScenarioNode node;
    node.id = "step";
    node.title = "step";
    node.procedure = std::move(procedure);
    node.arguments = std::move(arguments);
    scenario.steps.push_back(std::move(node));
    return scenario;
}

void procedureRuntimeContract()
{
    ScenarioEngine engine;
    registerUbsiProcedures(engine);

    ContractEquipment sensorEquipment;
    const auto sensorRun = engine.run(oneStep("ubsi.sensor_supply"),
        sensorEquipment, "p", "", false);
    require(sensorRun.verdict == RunVerdict::Incomplete,
        "rejected 350/450 mA automation must be INCOMPLETE, never a fake verdict");
    require(sensorEquipment.operations.empty(),
        "rejected sensor-supply procedure must perform no hardware operation");

    ContractEquipment yvpEquipment;
    const auto yvpRun = engine.run(oneStep("ubsi.yvp", {
        {"channel_count", "8"}, {"sample_count", "16"},
        {"parameter_group", "yvp_fast"},
        {"frequencies_hz", "0.15,20,250,500,1800,2000,4000"},
        {"gains_mv_per_pcl", "0.25,0.5,1,2,4,8,32"},
        {"routes_confirmed", "false"},
        {"yalk_value_model_confirmed", "false"}}),
        yvpEquipment, "p", "", false);
    require(yvpRun.verdict == RunVerdict::Incomplete,
        "uncommissioned YVP must be INCOMPLETE");
    for (const auto& operation : yvpEquipment.operations) {
        require(operation.rfind("signal.generator:", 0) != 0,
            "uncommissioned YVP must not enable or configure Rigol");
        require(operation.rfind("stand.switch_matrix:", 0) != 0,
            "uncommissioned YVP must not switch active YVP routes");
    }

    ContractEquipment supplyEquipment;
    const auto supplyRun = engine.run(oneStep("ubsi.supply_range", {
        {"voltage_points_v", "24,27,35"},
        {"maximum_total_current_a", "0.4"},
        {"supply_current_limit_a", "0.6"},
        {"voltage_tolerance_v", "0.5"},
        {"settle_ms", "0"}, {"restore_voltage_v", "27"}}),
        supplyEquipment, "p", "", false);
    require(supplyEquipment.hardwareCurrentLimit == 0.6,
        "hardware current limit must be configured independently at 0.6 A");
    require(supplyRun.verdict == RunVerdict::Fail,
        "0.41 A whole-block consumption at 35 V must fail the 0.4 A criterion");
    require(!supplyRun.steps.empty(), "supply contract must produce a step result");
    unsigned currentMeasurements = 0;
    unsigned failedCurrentMeasurements = 0;
    for (const auto& value : supplyRun.steps.front().measurements) {
        if (value.parameterKey == "ubsi.supply.total_current") {
            ++currentMeasurements;
            require(value.attributes.count("measurement_scope")
                    && value.attributes.at("measurement_scope") == "whole_ubsi",
                "current measurement must be explicitly scoped to whole UBSI");
            if (value.verdict == RunVerdict::Fail) ++failedCurrentMeasurements;
        }
    }
    require(currentMeasurements == 3,
        "whole-block current must be checked at 24, 27 and 35 V");
    require(failedCurrentMeasurements == 1,
        "only the synthetic 35 V / 0.41 A point must fail");
}

} // namespace

int main()
{
    try {
        yvpAddressContract();
        scenarioContract();
        yvpMathContract();
        procedureRuntimeContract();
        std::cout << "UBSI backend contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
