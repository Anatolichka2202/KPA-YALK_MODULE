#include "orbita_stand/scenario.h"
#include "orbita_stand/ubsi_procedures.h"
#include "orbita_stand/ubsi_yvp_math.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
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

std::size_t countOccurrences(const std::string& text, const std::string& value)
{
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(value, offset)) != std::string::npos) {
        ++count;
        offset += value.size();
    }
    return count;
}

std::string section(const std::string& text, const std::string& begin,
                    const std::string& end)
{
    const auto first = text.find(begin);
    if (first == std::string::npos) return {};
    const auto last = text.find(end, first + begin.size());
    return text.substr(first, last == std::string::npos ? std::string::npos : last - first);
}

std::string ytpCleanupSection(const std::string& yaml)
{
    const auto first = yaml.find("id: ytp_cleanup");
    if (first == std::string::npos) return {};
    const auto yvp = yaml.find("id: yvp", first);
    const auto power = yaml.find("id: power_off", first);
    std::size_t last = std::string::npos;
    if (yvp != std::string::npos) last = yvp;
    if (power != std::string::npos && (last == std::string::npos || power < last)) last = power;
    return yaml.substr(first, last == std::string::npos ? std::string::npos : last - first);
}

void sourceContract()
{
    const auto finalizer = readFile("station/src/ubsi_procedures_production_finalize.cpp");
    require(contains(finalizer, "registerProcedure(\"ubsi.isd_baseline\"")
            && contains(finalizer, "\"service_full_reset\"")
            && contains(finalizer, "\"session_state\"")
            && contains(finalizer, "\"owned_count\""),
        "Production ISD baseline contract is incomplete");
    require(contains(finalizer, "\"prepare_ytp_rokt\"")
            && contains(finalizer, "\"start_prepared_ytp_rokt\"")
            && contains(finalizer, "\"await_ytp_rokt\"")
            && contains(finalizer, "rokt_ytp68"),
        "Final YTP procedure is not the adapter/ROKT 68-byte path");

    const auto entry = readFile("station/src/ubsi_procedures_entry.cpp");
    require(contains(entry, "registerProductionFinalUbsiProcedures(engine);")
            && contains(entry, "registerProductionYvpProcedure(engine);"),
        "Final production overrides are not registered");

    const auto isdPlugin = readFile("station/plugins/isd_http_plugin.cpp");
    require(contains(isdPlugin, "instance.driver->safeStopAll();")
            && contains(isdPlugin, "command == \"service_full_reset\""),
        "ISD plugin lost targeted cleanup or explicit service reset");

    const auto overload = readFile("station/src/ubsi_procedures_rokt.cpp");
    require(contains(overload, "release_owner")
            && !contains(overload, "\"full_reset\""),
        "YALK overload cleanup must be targeted and must not use full reset");

    const auto transport = readFile("station/adapters/isd_http_transport.cpp");
    require(contains(transport, "There is deliberately no retry here"),
        "ISD transport lost the one-request/no-retry contract");

    const auto adapterPlugin = readFile("station/plugins/ktma_adapter_udp_plugin.cpp");
    const auto ulkTransport = readFile("station/adapters/ulk_udp_transport.cpp");
    const auto ulkHeader = readFile("station/include/orbita_stand/ulk_udp_transport.h");
    for (const auto& retired : {"start_yvp_probe", "start_yvp_channel_probe",
                                "read_yvp_raw", "read_yvp_channel_raw",
                                "YvpRokt136", "YvpChannelRokt132"}) {
        require(!contains(adapterPlugin, retired)
                && !contains(ulkTransport, retired)
                && !contains(ulkHeader, retired),
            std::string("Retired adapter YVP probe contract returned: ") + retired);
    }

    const auto legacy = readFile("station/src/ubsi_procedures.cpp");
    require(!contains(legacy, "\"full_reset\"")
            && !contains(legacy, "isd_switch_type")
            && !contains(legacy, "isd_route_channels"),
        "Retired UBSI source still contains obsolete global-reset/type7 lifecycle");
    const auto safeYalk = readFile("station/src/ubsi_procedures_isd_safe.cpp");
    require(contains(safeYalk, "\"run:\" + context.runId + \":yalk\"")
            && !contains(safeYalk, "return \"unscoped\""),
        "Production YALK ownership is not scoped to the active run");
}

void scenarioContract()
{
    const std::vector<std::string> production{
        "data/scenarios/ubsi_production_full.yaml",
        "data/scenarios/ubsi_production_power.yaml",
        "data/scenarios/ubsi_production_yalk.yaml",
        "data/scenarios/ubsi_production_ytp.yaml",
        "data/scenarios/ubsi_production_yvp.yaml",
        "data/scenarios/ubsi_ulk_combined_check.yaml",
        "data/scenarios/ubsi_yalk_contact_thresholds.yaml",
        "data/scenarios/ubsi_yalk_tu_5_6.yaml",
        "data/scenarios/ubsi_ytp_120_check.yaml",
        "data/scenarios/ubsi_ytp_tu_5_6.yaml",
        "data/scenarios/ubsi_tu_5_6.yaml",
    };
    for (const auto& path : production) {
        const auto yaml = readFile(path);
        const auto baseline = yaml.find("procedure: ubsi.isd_baseline");
        require(countOccurrences(yaml, "procedure: ubsi.isd_baseline") == 1,
            path + " must contain exactly one initial ISD baseline");
        require(baseline == yaml.find("procedure:"),
            path + " must start with the ISD baseline before any other procedure");
        require(!contains(yaml, "isd_switch_type: 7")
                && !contains(yaml, "isd_route_channels:"),
            path + " retains obsolete YTP ISD routing");
    }

    for (const auto& path : {
            "data/scenarios/ubsi_production_full.yaml",
            "data/scenarios/ubsi_production_ytp.yaml",
            "data/scenarios/ubsi_ulk_combined_check.yaml",
            "data/scenarios/ubsi_tu_5_6.yaml",
            "data/scenarios/ubsi_ytp_tu_5_6.yaml",
            "data/scenarios/ubsi_ytp_120_check.yaml"}) {
        const auto yaml = readFile(path);
        const auto start = section(yaml, "id: ytp_stream", "id: ytp_calibration");
        const auto cleanup = ytpCleanupSection(yaml);
        require(!start.empty() && contains(start, "ulk.parameter_source"),
            std::string(path) + " has no adapter YTP stream");
        require(!contains(start, "stand.switch_matrix")
                && !contains(start, "isd_switch_type")
                && !contains(start, "isd_route_channels"),
            std::string(path) + " YTP start still depends on ISD");
        require(!cleanup.empty() && contains(cleanup, "ulk.parameter_source")
                && !contains(cleanup, "stand.switch_matrix"),
            std::string(path) + " YTP cleanup must be adapter-only");
    }

    const auto yvp = readFile("data/scenarios/ubsi_production_yvp.yaml");
    require(contains(yvp, "procedure: ubsi.yvp")
            && contains(yvp, "gains_mv_per_pcl: 0.25,0.5,1,2,4,8,32")
            && contains(yvp, "frequencies_hz: 2,6,20,500,1800,2000,4000")
            && contains(yvp, "reference_frequency_hz: 500")
            && contains(yvp, "gain_tolerance_percent: 7")
            && contains(yvp, "attenuation_min_db: 20.0"),
        "Production YVP method changed unexpectedly");

    const auto combined = readFile("data/scenarios/ubsi_ulk_combined_check.yaml");
    require(!contains(combined, "procedure: ubsi.sensor_supply")
            && !contains(combined, "ubsi.external_evidence")
            && contains(combined, "maximum_total_current_a: 0.4")
            && contains(combined, "supply_current_limit_a: 0.6"),
        "Current TU backend reintroduced excluded/incorrect checks");
    require(!tuReferences(combined, "1.1.4.6")
            && !tuReferences(combined, "1.1.4.4"),
        "Current TU backend reintroduced checks excluded from the automated run");

    const auto legacyTu = readFile("data/scenarios/ubsi_tu_5_6.yaml");
    require(!tuReferences(legacyTu, "1.1.4.6")
            && !contains(legacyTu, "procedure: ubsi.sensor_supply"),
        "Legacy TU scenario reintroduced excluded 50 m or sensor-current automation");

    const auto traceability = readFile("data/scenarios/ubsi_tu_5_6_traceability.csv");
    require(contains(traceability, "1.1.4.6;5.6;нет;50-метровая линия"),
        "Traceability must explicitly record 1.1.4.6 as not checked");
}

void yvpScenarioRegressionContract()
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
    require(contains(standalone, "procedure: ubsi.yvp")
            && !contains(standalone, "procedure: yvp.enter_mode")
            && !contains(standalone, "ulk.parameter_source")
            && !contains(standalone, "catalog.parameter_resolver"),
        "Standalone YVP is not the direct V7+ISD production path");
    require(contains(standalone, "measure.reference_ac_voltage")
            && contains(standalone, "measure.reference_frequency")
            && contains(standalone, "stand.switch_matrix")
            && contains(standalone, "signal.generator"),
        "Standalone YVP is missing Rigol/ISD/V7 capabilities");
    require(!contains(standalone, "mapping_confirmed")
            && !contains(standalone, "active_outputs_confirmed"),
        "Confirmed YVP production map retained commissioning gates");
    require(contains(standalone, "input_1_contacts: 33")
            && contains(standalone, "channel_8_gain_contacts: 29,30,31,32")
            && contains(standalone, "settle_ms: 2000"),
        "YVP production input/KU map or settle contract changed");
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
                && contains(yaml, "attenuation_min_db: 20.0")
                && !contains(yaml, "procedure: yvp."),
            std::string(scenario) + " does not use the canonical YVP production method");
    }

    const auto cmake = readFile("station/CMakeLists.txt");
    const auto desktopCmake = readFile("apps/desktop/CMakeLists.txt");
    const auto registrar = readFile("station/src/ubsi_procedures_yvp_v7.cpp");
    require(!contains(cmake, "orbita_yvp_rokt_probe")
            && !contains(cmake, "orbita_yvp_v7_probe")
            && !contains(desktopCmake, "orbita_yvp_"),
        "Legacy YVP probe targets returned to the production build");
    require(!contains(registrar, "registerProcedure(\"yvp.")
            && contains(registrar, "registerProcedure(\"ubsi.yvp\""),
        "Production registrar must expose only ubsi.yvp");
}

class FakeEquipment final : public ICapabilityProvider {
public:
    bool hasCapability(const std::string&) const override { return true; }

    std::string invoke(const std::string& capability, const std::string& operation,
                       const std::map<std::string, std::string>&) override
    {
        calls.push_back(capability + ":" + operation);
        if (capability == "stand.switch_matrix" && operation == "service_full_reset") {
            ++fullResetCount;
            if (failReset) throw std::runtime_error("simulated ISD service timeout");
            return "status=ok\n";
        }
        if (capability == "stand.switch_matrix" && operation == "state") {
            return "status=ready\nsession_state=operational\nglobal_hardware_state=not_readable\nowned_count=0\n";
        }
        if (capability == "ulk.parameter_source" && operation == "start_record")
            return "status=ok\npath=records/test/ulk/frames.ulkbin\n";
        if (capability == "ulk.parameter_source" && operation == "prepare_ytp_rokt")
            return "status=prepared\nprotocol=rokt_ytp68\n";
        if (capability == "ulk.parameter_source" && operation == "start_prepared_ytp_rokt")
            return "status=started\nprotocol=rokt_ytp68\n";
        if (capability == "ulk.parameter_source" && operation == "await_ytp_rokt")
            return "status=ready\nprotocol=rokt_ytp68\nvalid_word_count=32\nfirst_sequence=2\n";
        return "status=ok\n";
    }

    void safeStopAll() noexcept override { safeStopCalled = true; }

    std::vector<std::string> calls;
    unsigned fullResetCount = 0;
    bool failReset = false;
    bool safeStopCalled = false;
};


class YvpContractEquipment final : public ICapabilityProvider {
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
        return "status=ready\n";
    }

    void safeStopAll() noexcept override { stopped = true; }

    std::vector<std::string> operations;
    std::vector<std::pair<std::string, std::map<std::string, std::string>>> requests;
    bool stopped = false;
    unsigned acVoltageReads = 0;
    unsigned frequencyReads = 0;
    double currentFrequency = 0.0;
    double acScale = 1.0;
    bool failIsdProbe = false;
};

ScenarioDefinition oneStep(const std::string& procedure,
                           std::map<std::string, std::string> arguments = {})
{
    ScenarioDefinition scenario;
    scenario.id = "contract";
    scenario.title = "contract";
    scenario.version = "1";
    scenario.catalogVersion = "1";
    scenario.objectType = "UBSI_468157_002";
    scenario.publicationState = PublicationState::Published;
    ScenarioNode node;
    node.id = "step";
    node.title = "step";
    node.tuRequirement = "contract";
    node.procedure = procedure;
    node.arguments = std::move(arguments);
    scenario.steps.push_back(std::move(node));
    return scenario;
}

void runtimeContract()
{
    ScenarioEngine engine;
    registerUbsiProcedures(engine);

    {
        FakeEquipment equipment;
        const auto result = engine.run(oneStep("ubsi.isd_baseline"), equipment,
                                       "profile", "serial", false);
        require(result.verdict == RunVerdict::Ok,
            "ISD baseline did not complete successfully");
        require(equipment.fullResetCount == 1,
            "ISD baseline did not issue exactly one service full reset");
    }

    {
        FakeEquipment equipment;
        equipment.failReset = true;
        const auto result = engine.run(oneStep("ubsi.isd_baseline"), equipment,
                                       "profile", "serial", false);
        require(result.verdict == RunVerdict::Error,
            "Failed ISD baseline must stop the run as technical error");
        require(equipment.fullResetCount == 1,
            "Failed baseline was retried implicitly");
    }

    {
        FakeEquipment equipment;
        const auto result = engine.run(oneStep("ytp.start_stream", {
                {"ytp_endpoint", "1"}, {"configure_settle_ms", "1"},
                {"stream_settle_ms", "1"}, {"timeout_ms", "50"}}),
            equipment, "profile", "serial", false);
        require(result.verdict == RunVerdict::Ok,
            "Adapter-only YTP start did not accept a fresh ROKT-68 frame");
        const std::vector<std::string> expected{
            "ulk.parameter_source:start_record",
            "ulk.parameter_source:prepare_ytp_rokt",
            "ulk.parameter_source:start_prepared_ytp_rokt",
            "ulk.parameter_source:await_ytp_rokt",
        };
        for (const auto& call : expected)
            require(std::find(equipment.calls.begin(), equipment.calls.end(), call)
                    != equipment.calls.end(), "Missing YTP call: " + call);
        for (const auto& call : equipment.calls)
            require(call.rfind("stand.switch_matrix:", 0) != 0,
                "YTP start unexpectedly invoked ISD: " + call);
    }

    {
        FakeEquipment equipment;
        const auto result = engine.run(oneStep("ytp.safe_cleanup"), equipment,
                                       "profile", "serial", false);
        require(result.verdict == RunVerdict::Ok,
            "Adapter-only YTP cleanup failed");
        require(std::find(equipment.calls.begin(), equipment.calls.end(),
                          "ulk.parameter_source:stop_stream") != equipment.calls.end()
                && std::find(equipment.calls.begin(), equipment.calls.end(),
                             "ulk.parameter_source:stop_record") != equipment.calls.end(),
            "YTP cleanup did not stop stream and recording");
        for (const auto& call : equipment.calls)
            require(call.rfind("stand.switch_matrix:", 0) != 0,
                "YTP cleanup unexpectedly invoked ISD: " + call);
    }
}


std::map<std::string, std::string> yvpArguments()
{
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
        args["measurement_" + std::to_string(channel) + "_contacts"] =
            std::to_string(measurements[channel - 1]);
        const unsigned first = (channel - 1) * 4 + 1;
        args["channel_" + std::to_string(channel) + "_gain_contacts"] =
            std::to_string(first) + "," + std::to_string(first + 1) + ","
            + std::to_string(first + 2) + "," + std::to_string(first + 3);
    }
    return args;
}

void yvpRuntimeRegressionContract()
{
    ScenarioEngine engine;
    registerUbsiProcedures(engine);

    YvpContractEquipment okEquipment;
    const auto okRun = engine.run(oneStep("ubsi.yvp", yvpArguments()),
                                  okEquipment, "p", "", false);
    require(okRun.verdict == RunVerdict::Ok,
        "Production YVP must return OK for measurements inside all limits");
    require(!okEquipment.operations.empty()
            && okEquipment.operations.front() == "stand.switch_matrix:probe",
        "ISD HTTP probe must run before active YVP operations");
    require(std::none_of(okEquipment.operations.begin(), okEquipment.operations.end(),
        [](const std::string& operation) {
            return operation.find("ulk.parameter_source") != std::string::npos
                || operation == "stand.switch_matrix:full_reset"
                || operation == "stand.switch_matrix:service_full_reset";
        }), "Production YVP must use neither adapter/ROKT nor global ISD reset");
    require(okEquipment.acVoltageReads == 8u * 7u * 7u,
        "Production YVP must measure the complete 8x7x7 matrix");
    require(okEquipment.frequencyReads == 8u * 7u * 5u,
        "V7 frequency must be diagnostic only and skipped below 10 Hz");

    YvpContractEquipment failEquipment;
    failEquipment.acScale = 1.08;
    const auto failRun = engine.run(oneStep("ubsi.yvp", yvpArguments()),
                                    failEquipment, "p", "", false);
    require(failRun.verdict == RunVerdict::Fail,
        "Production YVP must return FAIL when 500 Hz gain exceeds ±7%");

    YvpContractEquipment probeFailure;
    probeFailure.failIsdProbe = true;
    const auto errorRun = engine.run(oneStep("ubsi.yvp", yvpArguments()),
                                     probeFailure, "p", "", false);
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

void yvpMathContract()
{
    const double q = yvpChargePc(1000.0, 2.0);
    require(std::abs(q - 2000.0) < 1e-9, "Q=C*U calculation changed");
    const double gain = yvpGainMvPerPc(2.0, q);
    require(std::abs(gain - 1.0) < 1e-9, "YVP gain calculation changed");
    require(std::abs(yvpRelativeErrorPercent(gain, 1.0)) < 1e-9,
        "YVP gain relative error calculation changed");
    require(std::abs(yvpAfcPercent(1.05, 1.0) - 5.0) < 1e-9,
        "YVP AFC calculation changed");
    require(std::abs(yvpRelativeErrorPercent(1.07, 1.0)) <= 7.0 + 1e-9
            && std::abs(yvpRelativeErrorPercent(1.071, 1.0)) > 7.0,
        "Gain ±7% boundary changed");
    require(std::abs(yvpAfcPercent(1.10, 1.0)) <= 10.0 + 1e-9
            && std::abs(yvpAfcPercent(1.101, 1.0)) > 10.0,
        "AFC ±10% edge band changed");
    require(std::abs(yvpAfcPercent(1.05, 1.0)) <= 5.0 + 1e-9
            && std::abs(yvpAfcPercent(1.051, 1.0)) > 5.0,
        "AFC ±5% middle band changed");
    require(std::abs(yvpAttenuationDb(1.0, 0.1) - 20.0) < 1e-9,
        "YVP attenuation calculation changed");
    require(yvpStimulusVppForGain(0.25) == 8.0
            && yvpStimulusVppForGain(0.5) == 4.0
            && yvpStimulusVppForGain(1.0) == 2.0
            && yvpStimulusVppForGain(2.0) == 1.0
            && yvpStimulusVppForGain(4.0) == 0.5
            && yvpStimulusVppForGain(8.0) == 0.25
            && yvpStimulusVppForGain(32.0) == 0.0625,
        "YVP production stimulus table changed");
}

} // namespace

int main()
{
    try {
        sourceContract();
        scenarioContract();
        yvpScenarioRegressionContract();
        runtimeContract();
        yvpRuntimeRegressionContract();
        yvpMathContract();
        std::cout << "UBSI backend contract tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "UBSI backend contract test failed: " << error.what() << '\n';
        return 1;
    }
}
