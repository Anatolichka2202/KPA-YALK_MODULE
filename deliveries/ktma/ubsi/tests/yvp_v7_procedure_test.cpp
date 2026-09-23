#include "orbita_stand/scenario.h"
#include "orbita_stand/ubsi_procedures.h"

#include <algorithm>
#include <cmath>
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

class FakeYvpEquipment final : public ICapabilityProvider {
public:
    bool hasCapability(const std::string& capability) const override
    {
        return capabilities.count(capability) != 0;
    }

    bool resourceHasCapability(
        const std::string& resource,
        const std::string& capability) const override
    {
        static const std::map<std::string, std::set<std::string>> routes = {
            {"signal.primary", {"signal.generator"}},
            {"switch_matrix.primary", {"stand.switch_matrix"}},
            {"measure.reference", {
                "measure.reference_ac_voltage", "measure.reference_frequency"}},
        };
        const auto found = routes.find(resource);
        return found != routes.end() && found->second.count(capability)
            && hasCapability(capability);
    }

    std::string invokeResource(
        const std::string& resource,
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments) override
    {
        if (!resourceHasCapability(resource, capability))
            throw std::runtime_error("unexpected resource route");
        resourceOperations.push_back(resource + ":" + capability + ":" + operation);
        return invoke(capability, operation, arguments);
    }

    std::string invoke(
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments) override
    {
        operations.push_back(capability + ":" + operation);
        if (capability == "stand.switch_matrix") {
            if (operation == "release_owner") {
                ++releaseCount;
                return "status=ready\nowned_count=0\n";
            }
            if (operation == "switch" || operation == "analog") {
                require(arguments.count("owner") != 0, "YVP ISD mutation has no owner");
                require(arguments.at("owner").find(":yvp") != std::string::npos,
                    "YVP ISD mutation has wrong owner");
                return "status=ready\n";
            }
            if (operation == "service_full_reset" || operation == "full_reset") {
                ++globalResetCount;
                return "status=ready\n";
            }
        }
        if (capability == "signal.generator") {
            if (operation == "set_sine") {
                frequency = std::stod(arguments.at("frequency_hz"));
                inputVpp = std::stod(arguments.at("amplitude_vpp"));
                return "status=ready\n";
            }
            if (operation == "output") {
                outputEnabled = arguments.at("enabled") == "true";
                if (outputEnabled) ++outputEnableCount;
                return "status=ready\n";
            }
        }
        if (capability == "measure.reference_ac_voltage") {
            if (operation == "reconnect") {
                ++reconnectCount;
                return "status=ready\noperation=reconnect\n";
            }
            if (operation == "read_ac_voltage") {
                if (failFirstRead) {
                    failFirstRead = false;
                    throw std::runtime_error("synthetic VISA timeout");
                }
                // For K=1 and C=1000 pF the verified procedure drives 2 Vpp.
                // A flat 2 Vpp output means Kmeas=1 mV/pC. At 4000 Hz use
                // exactly 20 dB attenuation to exercise the normative edge.
                const double outputVpp = std::abs(frequency - 4000.0) < 1e-9 ? 0.2 : 2.0;
                const double vrms = outputVpp / (2.0 * std::sqrt(2.0));
                return "status=ready\nvolts=" + std::to_string(vrms) + "\n";
            }
        }
        if (capability == "measure.reference_frequency" && operation == "read_frequency") {
            return "status=ready\nhertz=" + std::to_string(frequency) + "\n";
        }
        throw std::runtime_error("unexpected operation " + capability + ":" + operation);
    }

    void safeStopAll() noexcept override
    {
        outputEnabled = false;
        stopped = true;
    }

    std::set<std::string> capabilities{
        "signal.generator", "stand.switch_matrix",
        "measure.reference_ac_voltage", "measure.reference_frequency"};
    std::vector<std::string> operations;
    std::vector<std::string> resourceOperations;
    double frequency = 0.0;
    double inputVpp = 0.0;
    bool outputEnabled = false;
    bool failFirstRead = true;
    bool stopped = false;
    unsigned reconnectCount = 0;
    unsigned releaseCount = 0;
    unsigned globalResetCount = 0;
    unsigned outputEnableCount = 0;
};

ScenarioDefinition scenario()
{
    ScenarioDefinition scenario;
    scenario.id = "test.yvp.v7";
    scenario.title = "YVP V7/ISD regression";
    scenario.version = "1";
    scenario.catalogVersion = "1";
    scenario.objectType = "UBSI_468157_002";
    scenario.publicationState = PublicationState::Published;

    ScenarioNode node;
    node.id = "yvp";
    node.title = "YVP-8";
    node.tuRequirement = "1.1.4.7, 1.1.4.8, 1.1.4.14";
    node.procedure = "ubsi.yvp";
    node.requiredResources = {
        {"signal.primary", "signal.generator"},
        {"switch_matrix.primary", "stand.switch_matrix"},
        {"measure.reference", "measure.reference_ac_voltage"},
        {"measure.reference", "measure.reference_frequency"},
    };
    node.arguments = {
        {"channel_count", "8"},
        {"gains_mv_per_pcl", "1"},
        {"frequencies_hz", "2,6,20,500,1800,2000,4000"},
        {"afc_gain_mv_per_pcl", "1"},
        {"reference_frequency_hz", "500"},
        {"coupling_capacitance_pf", "1000"},
        {"settle_ms", "1"},
        {"v7_read_retries", "2"},
        {"v7_retry_delay_ms", "1"},
        {"gain_tolerance_percent", "7"},
        {"attenuation_min_db", "20"},
        {"active_outputs_confirmed", "true"},
        {"mapping_confirmed", "true"},
        {"input_switch_type", "2"},
        {"gain_switch_type", "2"},
        {"measurement_analog_type", "1"},
        {"measurement_switch_type", "3"},
        {"input_1_contacts", "33"}, {"input_2_contacts", "34"},
        {"input_3_contacts", "35"}, {"input_4_contacts", "36"},
        {"input_5_contacts", "37"}, {"input_6_contacts", "38"},
        {"input_7_contacts", "39"}, {"input_8_contacts", "40"},
        {"measurement_1_contacts", "44"}, {"measurement_2_contacts", "29"},
        {"measurement_3_contacts", "30"}, {"measurement_4_contacts", "31"},
        {"measurement_5_contacts", "71"}, {"measurement_6_contacts", "72"},
        {"measurement_7_contacts", "88"}, {"measurement_8_contacts", "73"},
        {"channel_1_gain_contacts", "1,2,3,4"},
        {"channel_2_gain_contacts", "5,6,7,8"},
        {"channel_3_gain_contacts", "9,10,11,12"},
        {"channel_4_gain_contacts", "13,14,15,16"},
        {"channel_5_gain_contacts", "17,18,19,20"},
        {"channel_6_gain_contacts", "21,22,23,24"},
        {"channel_7_gain_contacts", "25,26,27,28"},
        {"channel_8_gain_contacts", "29,30,31,32"},
        {"gain_1_bits", "2"},
    };
    scenario.steps.push_back(std::move(node));
    return scenario;
}

} // namespace

int main()
{
    try {
        ScenarioEngine engine;
        registerUbsiProcedures(engine);
        FakeYvpEquipment equipment;
        const auto run = engine.run(scenario(), equipment, "profile", "SN", false);

        require(run.verdict == RunVerdict::Ok,
            "verified V7/ISD YVP regression must complete OK");
        require(run.steps.size() == 1 && run.steps.front().measurements.size() == 56,
            "YVP must produce one gain + five AFC + one attenuation result for all 8 channels");
        require(equipment.reconnectCount == 1,
            "one synthetic V7 timeout must reopen the VISA session exactly once");
        require(equipment.globalResetCount == 0,
            "YVP must never perform a global ISD reset");
        require(equipment.releaseCount >= 9,
            "YVP must release only its owner-scoped ISD routes at safety boundaries");
        require(!equipment.outputEnabled && equipment.stopped,
            "YVP run must finish with generator output off and station safe-stop executed");
        require(equipment.outputEnableCount == 56,
            "YVP must preserve the stimulus while retrying V7 instead of re-running the point");
        require(std::all_of(equipment.resourceOperations.begin(),
            equipment.resourceOperations.end(), [](const std::string& operation) {
                return operation.rfind("signal.primary:", 0) == 0
                    || operation.rfind("switch_matrix.primary:", 0) == 0
                    || operation.rfind("measure.reference:", 0) == 0;
            }), "YVP physical operations escaped declared delivery resources");

        std::cout << "YVP V7/ISD procedure regression OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "YVP V7/ISD procedure regression failed: " << error.what() << '\n';
        return 1;
    }
}
