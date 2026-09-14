#include "orbita_stand/scenario.h"
#include "orbita_stand/ubsi_procedures.h"
#include "orbita_stand/ubsi_yvp_math.h"

#include <algorithm>
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

void yvpScenarioContract()
{
    const auto standalone = readFile("data/scenarios/ubsi_production_yvp.yaml");
    require(contains(standalone, "procedure: ubsi.yvp"),
        "standalone YVP production must call the production YVP alias");
    require(!contains(standalone, "procedure: yvp.enter_mode"),
        "V7+ISD production YVP must not enter the adapter/ROKT measurement path");
    require(!contains(standalone, "ulk.parameter_source")
            && !contains(standalone, "catalog.parameter_resolver"),
        "V7+ISD production YVP must not require the adapter or YALK catalog decoder");
    require(contains(standalone, "measure.reference_ac_voltage")
            && contains(standalone, "measure.reference_frequency")
            && contains(standalone, "stand.switch_matrix")
            && contains(standalone, "signal.generator"),
        "V7+ISD production YVP must require Rigol, ISD and V7");
    require(contains(standalone, "mapping_confirmed: false"),
        "YVP production must stay fail-safe until the ISD E3 map is commissioned");
    require(contains(standalone, "gains_mv_per_pcl: 0.25,0.5,1,2,4,8,32"),
        "YVP method must use the seven confirmed gain values");
    require(contains(standalone, "frequencies_hz: 0.15,20,250,500,1800,2000,4000"),
        "YVP method must retain the confirmed frequency set");
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
    require(contains(combined, "maximum_total_current_a: 0.4"),
        "whole-UBSI current criterion must be 0.4 A");
    require(contains(combined, "supply_current_limit_a: 0.6"),
        "hardware current limit must remain separate from the 0.4 A criterion");

    const auto legacy = readFile("data/scenarios/ubsi_tu_5_6.yaml");
    require(!tuReferences(legacy, "1.1.4.6"),
        "legacy trace scenario must not reintroduce the 50 m check");
    require(!contains(legacy, "procedure: ubsi.sensor_supply"),
        "legacy trace scenario must not reintroduce 350/450 mA automation");

    const auto traceability = readFile("data/scenarios/ubsi_tu_5_6_traceability.csv");
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
    require(yvpStimulusVppForGain(0.25) == 8.0, "0.25 mV/pC stimulus must be 8 Vpp");
    require(yvpStimulusVppForGain(0.5) == 4.0, "0.5 mV/pC stimulus must be 4 Vpp");
    require(yvpStimulusVppForGain(1.0) == 2.0, "1 mV/pC stimulus must be 2 Vpp");
    require(yvpStimulusVppForGain(2.0) == 1.0, ">1 mV/pC stimulus must be 1 Vpp");
}

class ContractEquipment final : public ICapabilityProvider {
public:
    bool hasCapability(const std::string&) const override { return true; }

    std::string invoke(const std::string& capability, const std::string& operation,
                       const std::map<std::string, std::string>& arguments) override
    {
        operations.push_back(capability + ":" + operation);
        if (capability == "ulk.parameter_source" && operation == "start_yvp_probe") {
            ++yvpStarts;
            return "status=capturing\n";
        }
        if (capability == "ulk.parameter_source" && operation == "stats")
            return "status=ready\nlast_sequence=1\nyvp_rokt136=1\nunknown=0\ndropped=0\n";
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
        if (capability == "measure.reference_ac_voltage"
            && operation == "read_ac_voltage") {
            ++acVoltageReads;
            return "status=ready\nvolts=1.41421356237\n";
        }
        if (capability == "measure.reference_frequency"
            && operation == "read_frequency") {
            ++frequencyReads;
            return "status=ready\nhertz=20\n";
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
    unsigned yvpStarts = 0;
    unsigned acVoltageReads = 0;
    unsigned frequencyReads = 0;
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
    node.tuRequirement = "5.6";
    node.procedure = std::move(procedure);
    node.arguments = std::move(arguments);
    scenario.steps.push_back(std::move(node));
    return scenario;
}

void procedureRuntimeContract()
{
    ScenarioEngine engine;
    registerUbsiProcedures(engine);

    ContractEquipment yvpEquipment;
    const auto yvpRun = engine.run(oneStep("ubsi.yvp", {
        {"channel_count", "8"},
        {"gains_mv_per_pcl", "0.25,0.5,1,2,4,8,32"},
        {"frequencies_hz", "0.15,20,250,500,1800,2000,4000"},
        {"mapping_confirmed", "false"},
        {"active_outputs_confirmed", "true"}}), yvpEquipment, "p", "", false);
    require(yvpRun.verdict == RunVerdict::Incomplete,
        "V7+ISD YVP must remain INCOMPLETE until the ISD map is confirmed");
    require(yvpEquipment.operations.empty(),
        "V7+ISD YVP must perform no hardware operation before mapping_confirmed=true");

    ContractEquipment roktEquipment;
    const auto roktRun = engine.run(oneStep("yvp.enter_mode", {
        {"yvp_cell", "1"}, {"timeout_ms", "100"}}), roktEquipment, "p", "", false);
    require(roktRun.verdict == RunVerdict::Ok && roktEquipment.yvpStarts == 1,
        "ROKT commissioning path must remain available separately");
    require(std::count(roktEquipment.operations.begin(), roktEquipment.operations.end(),
                "ulk.parameter_source:start_yvp_probe") == 1,
        "ROKT commissioning must not be selected by ubsi.yvp");

    ContractEquipment roktBackendEquipment;
    const auto roktBackendRun = engine.run(oneStep("yvp.rokt", {
        {"channel_count", "8"}, {"yvp_cell", "1"}}),
        roktBackendEquipment, "p", "", false);
    require(roktBackendRun.verdict == RunVerdict::Incomplete,
        "The retained adapter/ROKT backend must remain a diagnostic path");
    require(std::count(roktBackendEquipment.operations.begin(),
                       roktBackendEquipment.operations.end(),
                       "ulk.parameter_source:start_yvp_channel_probe") == 8,
        "The explicit yvp.rokt alias must execute the retained channel probe");

    ContractEquipment v7Equipment;
    const auto v7Run = engine.run(oneStep("yvp.v7_isd", {
        {"channel_count", "8"},
        {"commissioning_channels", "1"},
        {"gains_mv_per_pcl", "1"},
        {"frequencies_hz", "0.15,20"},
        {"mapping_confirmed", "true"},
        {"active_outputs_confirmed", "true"},
        {"input_switch_type", "2"},
        {"gain_switch_type", "2"},
        {"measurement_switch_type", "2"},
        {"input_1_contacts", "101"},
        {"input_2_contacts", "102"},
        {"input_3_contacts", "103"},
        {"input_4_contacts", "104"},
        {"input_5_contacts", "105"},
        {"input_6_contacts", "106"},
        {"input_7_contacts", "107"},
        {"input_8_contacts", "108"},
        {"measurement_1_contacts", "201"},
        {"measurement_2_contacts", "202"},
        {"measurement_3_contacts", "203"},
        {"measurement_4_contacts", "204"},
        {"measurement_5_contacts", "205"},
        {"measurement_6_contacts", "206"},
        {"measurement_7_contacts", "207"},
        {"measurement_8_contacts", "208"},
        {"gain_1_contacts", "none"},
        {"settle_ms", "0"}}), v7Equipment, "p", "", false);
    require(v7Run.verdict == RunVerdict::Incomplete
                && v7Run.steps.front().measurements.size() == 2,
        "V7+ISD commissioning filter must execute one channel and retain both points");
    require(v7Equipment.acVoltageReads == 2 && v7Equipment.frequencyReads == 1,
        "V7 must skip frequency read at 0.15 Hz and read it at 20 Hz");
    require(std::count(v7Equipment.operations.begin(), v7Equipment.operations.end(),
                       "signal.generator:output") >= 4,
        "V7+ISD commissioning must switch Rigol safely around both points");
    for (const auto& measurement : v7Run.steps.front().measurements) {
        if (measurement.attributes.at("set_frequency_hz") == "0.150000") {
            require(measurement.attributes.at("frequency_verification")
                        == "unavailable_by_v7"
                        && measurement.attributes.at("measured_frequency_hz").empty(),
                "0.15 Hz must remain unverified by V7 frequency readout");
        }
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
        "hardware current limit must be configured independently at 0.6 A: " + supplyRun.steps.front().message);
    require(supplyRun.verdict == RunVerdict::Fail,
        "0.41 A whole-block consumption at 35 V must fail the 0.4 A criterion");
    require(!supplyRun.steps.empty(), "supply contract must produce a step result");
    unsigned currentMeasurements = 0;
    unsigned failedCurrentMeasurements = 0;
    for (const auto& value : supplyRun.steps.front().measurements) {
        if (value.parameterKey == "ubsi.supply.total_current") {
            ++currentMeasurements;
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
        yvpScenarioContract();
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
