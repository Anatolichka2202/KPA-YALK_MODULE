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
    const std::vector<std::string> measurementMap{
        "measurement_1_contacts: 44",
        "measurement_2_contacts: 29",
        "measurement_3_contacts: 30",
        "measurement_4_contacts: 31",
        "measurement_5_contacts: 71",
        "measurement_6_contacts: 72",
        "measurement_7_contacts: 88",
        "measurement_8_contacts: 73",
    };
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
    require(!contains(standalone, "mapping_confirmed")
            && !contains(standalone, "active_outputs_confirmed"),
        "Confirmed production map must not retain commissioning gates");
    require(contains(standalone, "input_1_contacts: 33")
            && contains(standalone, "channel_8_gain_contacts: 29,30,31,32"),
        "YVP production must retain explicit input, output and per-channel KU maps");
    for (const auto& entry : measurementMap)
        require(contains(standalone, entry), "Standalone YVP map is incomplete: " + entry);

    for (const auto& scenario : {
            "data/scenarios/ubsi_production_full.yaml",
            "data/scenarios/ubsi_tu_5_6.yaml",
            "data/scenarios/ubsi_ulk_combined_check.yaml"}) {
        const auto yaml = readFile(scenario);
        for (const auto& entry : measurementMap)
            require(contains(yaml, entry), std::string(scenario) + " YVP map differs: " + entry);
        require(contains(yaml, "frequencies_hz: 2,6,20,500,1800,2000,4000")
                && contains(yaml, "reference_frequency_hz: 500")
                && contains(yaml, "gain_tolerance_percent: 7")
                && contains(yaml, "attenuation_min_db: 20.0"),
            std::string(scenario) + " does not use the common production YVP method");
        require(!contains(yaml, "procedure: yvp.")
                && !contains(yaml, "active_outputs_confirmed"),
            std::string(scenario) + " retains a legacy/commissioning YVP contract");
    }
    require(contains(standalone, "gains_mv_per_pcl: 0.25,0.5,1,2,4,8,32"),
        "YVP method must use the seven confirmed gain values");
    require(contains(standalone, "frequencies_hz: 2,6,20,500,1800,2000,4000"),
        "YVP method must use the production frequency set");
    require(contains(standalone, "settle_ms: 2000"),
        "YVP must allow the live generator/YVP/V7 path to settle before measurement");

    const auto cmake = readFile("station/CMakeLists.txt");
    const auto desktopCmake = readFile("apps/desktop/CMakeLists.txt");
    const auto registrar = readFile("station/src/ubsi_procedures_yvp_v7.cpp");
    const auto isdPlugin = readFile("station/plugins/isd_http_plugin.cpp");
    const auto equipmentProbe = readFile("station/tools/equipment_probe.cpp");
    require(!contains(cmake, "orbita_yvp_rokt_probe")
            && !contains(cmake, "orbita_yvp_v7_probe")
            && !contains(desktopCmake, "orbita_yvp_"),
        "Legacy YVP probe targets remain in the production build");
    require(!contains(registrar, "registerProcedure(\"yvp.")
            && contains(registrar, "registerProcedure(\"ubsi.yvp\""),
        "Production registrar must expose only ubsi.yvp");
    require(contains(isdPlugin, "void safeStop(void*) {}")
            && !contains(equipmentProbe, "full_reset"),
        "Generic ISD/equipment-probe cleanup can still issue type=4");
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
    require(std::abs(yvpRelativeErrorPercent(1.07, 1.0)) <= 7.0 + 1e-9
            && std::abs(yvpRelativeErrorPercent(1.071, 1.0)) > 7.0,
        "Gain ±7% boundary is wrong");
    require(std::abs(yvpAfcPercent(1.10, 1.0)) <= 10.0 + 1e-9
            && std::abs(yvpAfcPercent(1.101, 1.0)) > 10.0,
        "AFC ±10% edge band is wrong");
    require(std::abs(yvpAfcPercent(1.05, 1.0)) <= 5.0 + 1e-9
            && std::abs(yvpAfcPercent(1.051, 1.0)) > 5.0,
        "AFC ±5% middle band is wrong");
    require(std::abs(yvpAttenuationDb(1.0, 0.1) - 20.0) < 1e-9,
        "attenuation calculation is wrong");
    require(yvpStimulusVppForGain(0.25) == 8.0, "0.25 mV/pC stimulus must be 8 Vpp");
    require(yvpStimulusVppForGain(0.5) == 4.0, "0.5 mV/pC stimulus must be 4 Vpp");
    require(yvpStimulusVppForGain(1.0) == 2.0, "1 mV/pC stimulus must be 2 Vpp");
    require(yvpStimulusVppForGain(2.0) == 1.0, "2 mV/pC stimulus must be 1 Vpp");
    require(yvpStimulusVppForGain(4.0) == 0.5, "4 mV/pC stimulus must be 0.5 Vpp");
    require(yvpStimulusVppForGain(8.0) == 0.25, "8 mV/pC stimulus must be 0.25 Vpp");
    require(yvpStimulusVppForGain(32.0) == 0.0625, "32 mV/pC stimulus must be 0.0625 Vpp");
}

class ContractEquipment final : public ICapabilityProvider {
public:
    bool hasCapability(const std::string&) const override { return true; }

    std::string invoke(const std::string& capability, const std::string& operation,
                       const std::map<std::string, std::string>& arguments) override
    {
        operations.push_back(capability + ":" + operation);
        requests.push_back({capability + ":" + operation, arguments});
        if (capability == "stand.switch_matrix" && operation == "probe") {
            if (failIsdProbe) throw std::runtime_error("ISD unavailable");
            return "status=ready\nalive=1\n";
        }
        if (capability == "signal.generator" && operation == "set_sine") {
            currentFrequency = std::stod(arguments.at("frequency_hz"));
            return "status=ok\n";
        }
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
            const double attenuation = std::abs(currentFrequency - 4000.0) < 0.01
                ? 0.09 : 1.0;
            return "status=ready\nvolts="
                + std::to_string(0.7071067811865476 * attenuation * acScale) + "\n";
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
    std::vector<std::pair<std::string, std::map<std::string, std::string>>> requests;
    double supplyVoltage = 27.0;
    double hardwareCurrentLimit = 0.0;
    bool outputEnabled = false;
    bool stopped = false;
    unsigned yvpStarts = 0;
    unsigned acVoltageReads = 0;
    unsigned frequencyReads = 0;
    double currentFrequency = 0.0;
    double acScale = 1.0;
    bool failIsdProbe = false;
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

    auto arguments = [] {
        std::map<std::string, std::string> args{
            {"channel_count", "8"}, {"gains_mv_per_pcl", "0.25,0.5,1,2,4,8,32"},
            {"frequencies_hz", "2,6,20,500,1800,2000,4000"},
            {"coupling_capacitance_pf", "1000"}, {"reference_frequency_hz", "500"},
            {"gain_tolerance_percent", "7"}, {"attenuation_min_db", "20"},
            {"settle_ms", "0"}, {"input_switch_type", "2"},
            {"gain_switch_type", "2"}, {"measurement_switch_type", "3"},
            {"measurement_analog_type", "1"},
            {"gain_0_25_bits", "none"}, {"gain_0_5_bits", "1"},
            {"gain_1_bits", "2"}, {"gain_2_bits", "3"},
            {"gain_4_bits", "1,3"}, {"gain_8_bits", "4"}, {"gain_32_bits", "2,4"}
        };
        const unsigned measurements[] = {44,29,30,31,71,72,88,73};
        for (unsigned channel = 1; channel <= 8; ++channel) {
            args["input_" + std::to_string(channel) + "_contacts"] = std::to_string(32 + channel);
            args["measurement_" + std::to_string(channel) + "_contacts"] = std::to_string(measurements[channel - 1]);
            const unsigned first = (channel - 1) * 4 + 1;
            args["channel_" + std::to_string(channel) + "_gain_contacts"] =
                std::to_string(first) + "," + std::to_string(first + 1) + "," +
                std::to_string(first + 2) + "," + std::to_string(first + 3);
        }
        return args;
    };

    ContractEquipment okEquipment;
    const auto okRun = engine.run(oneStep("ubsi.yvp", arguments()), okEquipment, "p", "", false);
    require(okRun.verdict == RunVerdict::Ok,
        "Production YVP must return OK for measurements inside all limits");
    require(!okEquipment.operations.empty() && okEquipment.operations.front() == "stand.switch_matrix:probe",
        "ISD HTTP probe must run before active YVP operations");
    require(std::none_of(okEquipment.operations.begin(), okEquipment.operations.end(),
        [](const std::string& operation) {
            return operation.find("ulk.parameter_source") != std::string::npos
                || operation == "stand.switch_matrix:full_reset";
        }), "Production YVP must use neither adapter/ROKT nor ISD full reset");
    require(okEquipment.acVoltageReads == 8u * 7u * 7u,
        "Production YVP must measure the complete 8x7x7 matrix");
    require(okEquipment.frequencyReads == 8u * 7u * 5u,
        "V7 frequency must be diagnostic only and skipped below 10 Hz");

    ContractEquipment failEquipment;
    failEquipment.acScale = 1.08;
    const auto failRun = engine.run(oneStep("ubsi.yvp", arguments()), failEquipment, "p", "", false);
    require(failRun.verdict == RunVerdict::Fail,
        "Production YVP must return FAIL when 500 Hz gain exceeds ±7%");

    ContractEquipment probeFailure;
    probeFailure.failIsdProbe = true;
    const auto errorRun = engine.run(oneStep("ubsi.yvp", arguments()), probeFailure, "p", "", false);
    require(errorRun.verdict == RunVerdict::Error
            && std::count(probeFailure.operations.begin(), probeFailure.operations.end(),
                          "signal.generator:set_sine") == 0,
        "ISD preflight failure must be a technical ERROR before active stimulus");

    for (const auto& legacy : {"yvp.v7_isd", "yvp.enter_mode", "yvp.safe_cleanup",
                               "yvp.rokt", "yvp.rokt.enter_mode",
                               "yvp.rokt.channels", "yvp.rokt.safe_cleanup"}) {
        require(!engine.validate(oneStep(legacy)).empty(),
            std::string("Legacy YVP procedure remains registered: ") + legacy);
    }

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
