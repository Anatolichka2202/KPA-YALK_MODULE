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
    };
    for (const auto& path : production) {
        const auto yaml = readFile(path);
        require(countOccurrences(yaml, "procedure: ubsi.isd_baseline") == 1,
            path + " must contain exactly one initial ISD baseline");
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

void yvpMathContract()
{
    const double q = yvpChargePc(1000.0, 2.0);
    require(std::abs(q - 2000.0) < 1e-9, "Q=C*U calculation changed");
    require(std::abs(yvpGainMvPerPc(2.0, q) - 1.0) < 1e-9,
        "YVP gain calculation changed");
    require(std::abs(yvpAfcPercent(1.05, 1.0) - 5.0) < 1e-9,
        "YVP AFC calculation changed");
    require(std::abs(yvpAttenuationDb(1.0, 0.1) - 20.0) < 1e-9,
        "YVP attenuation calculation changed");
    require(yvpStimulusVppForGain(0.25) == 8.0
            && yvpStimulusVppForGain(32.0) == 0.0625,
        "YVP production stimulus table changed");
}

} // namespace

int main()
{
    try {
        sourceContract();
        scenarioContract();
        runtimeContract();
        yvpMathContract();
        std::cout << "UBSI backend contract tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "UBSI backend contract test failed: " << error.what() << '\n';
        return 1;
    }
}
