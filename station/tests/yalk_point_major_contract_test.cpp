#include "orbita_stand/ubsi_procedures.h"

#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace orbita::stand;

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

class PointMajorEquipment final : public ICapabilityProvider {
public:
    bool hasCapability(const std::string&) const override { return true; }

    std::string invoke(const std::string& capability, const std::string& operation,
                       const std::map<std::string, std::string>& arguments) override
    {
        if (capability == "catalog.parameter_resolver" && operation == "resolve") {
            const std::string group = arguments.at("parameter_group");
            const unsigned channel = static_cast<unsigned>(std::stoul(arguments.at("channel_index")));
            unsigned locator = channel + 1;
            std::string route = "yalk_voltage";
            if (group == "yalk_calibration_zero") { locator = 97; route = "calibration"; }
            else if (group == "yalk_calibration_full") { locator = 99; route = "calibration"; }
            else if (group != "yalk_voltage" && group != "yalk_signal")
                throw std::runtime_error("unexpected catalog group: " + group);

            return "source=ulk.parameter_source\n"
                   "locator_type=ulk_address\n"
                   "locator=" + std::to_string(locator) + "\n"
                   "stream_id=yalk\n"
                   "word_index=" + std::to_string(locator - 1) + "\n"
                   "mask=1023\n"
                   "shift=0\n"
                   "mode=0\n"
                   "conversion_id=yalk\n"
                   "stimulus_route=" + route + "\n"
                   "stimulus_offset=" + std::to_string(channel) + "\n"
                   "confirmed=true\n";
        }

        if (capability == "stand.switch_matrix" && operation == "yalk_set_voltage") {
            activeAddress = static_cast<unsigned>(std::stoul(arguments.at("ulk_address")));
            activeVoltage = std::stod(arguments.at("volts"));
            return "status=ok\n";
        }
        if (capability == "stand.switch_matrix" && operation == "yalk_output_off") {
            const unsigned address = static_cast<unsigned>(std::stoul(arguments.at("ulk_address")));
            if (address == activeAddress) activeVoltage = 0.0;
            return "status=ok\n";
        }

        if (capability == "measure.reference_voltage" && operation == "read_voltage")
            return "status=ready\nvolts=" + std::to_string(activeVoltage) + "\n";

        if (capability == "ulk.parameter_source" && operation == "stats")
            return "status=ready\nlast_sequence=" + std::to_string(sequence) + "\n";

        if (capability == "ulk.parameter_source" && operation == "read_channel") {
            const unsigned address = static_cast<unsigned>(std::stoul(arguments.at("ulk_address")));
            double code = 100.0;
            bool signal = false;
            if (address == 99) code = 900.0;
            else if (address == 97) code = 100.0;
            else if (address == activeAddress) {
                code = 100.0 + activeVoltage / 6.2 * 800.0;
                signal = activeVoltage >= 2.0;
            }
            ++sequence;
            return "status=ready\n"
                   "raw_mean=" + std::to_string(code) + "\n"
                   "analog_code_mean=" + std::to_string(code) + "\n"
                   "signal=" + std::string(signal ? "1" : "0") + "\n"
                   "first_sequence=" + std::to_string(sequence) + "\n"
                   "last_sequence=" + std::to_string(sequence) + "\n"
                   "raw_samples=" + std::to_string(code) + "\n"
                   "analog_code_samples=" + std::to_string(code) + "\n";
        }

        return "status=ready\n";
    }

    void safeStopAll() noexcept override {}

    unsigned activeAddress = 0;
    double activeVoltage = 0.0;
    unsigned sequence = 1;
};

ScenarioDefinition pointMajorScenario()
{
    ScenarioDefinition scenario;
    scenario.id = "point-major-contract";
    scenario.title = "point-major-contract";
    scenario.version = "1";
    scenario.catalogVersion = "1";
    scenario.publicationState = PublicationState::Published;

    ScenarioNode calibration;
    calibration.id = "calibration";
    calibration.title = "calibration";
    calibration.tuRequirement = "contract";
    calibration.procedure = "yalk.read_calibration";
    calibration.arguments = {
        {"channel_count", "3"},
        {"sample_count", "1"},
        {"settle_ms", "0"},
        {"full_voltage", "6.2"}};

    ScenarioNode scan;
    scan.id = "scan";
    scan.title = "scan";
    scan.tuRequirement = "contract";
    scan.procedure = "yalk.check_channels";
    scan.arguments = {
        {"channel_count", "3"},
        {"point_volts", "0,3.1,6.2"},
        {"sample_count", "1"},
        {"settle_ms", "0"},
        {"channel_off_settle_ms", "0"},
        {"full_scale_v", "6.2"},
        {"tolerance_percent_fs", "0.5"}};

    scenario.steps = {calibration, scan};
    return scenario;
}

} // namespace

int main()
{
    try {
        ScenarioEngine engine;
        registerUbsiProcedures(engine);
        PointMajorEquipment equipment;
        std::vector<std::pair<int, int>> order;
        std::vector<std::string> scanOrders;

        const auto result = engine.run(pointMajorScenario(), equipment, "contract", "SN", false,
            [&](const RunEvent& event) {
                if (event.nodeId != "scan" || event.stage != "MEASUREMENT") return;
                const auto point = event.data.find("point_index");
                const auto channel = event.data.find("channel_index");
                const auto scanOrder = event.data.find("scan_order");
                if (point == event.data.end() || channel == event.data.end()) return;
                order.emplace_back(std::stoi(point->second), std::stoi(channel->second));
                scanOrders.push_back(scanOrder == event.data.end() ? std::string() : scanOrder->second);
            });

        require(result.verdict == RunVerdict::Ok, "point-major contract scenario must pass");
        const std::vector<std::pair<int, int>> expected = {
            {1,1}, {1,2}, {1,3},
            {2,1}, {2,2}, {2,3},
            {3,1}, {3,2}, {3,3}};
        require(order == expected,
            "YALK scan order must be point-major: all channels at 0 V, then 3.1 V, then 6.2 V");
        require(scanOrders.size() == expected.size(), "every analogue event must expose scan_order");
        for (const auto& value : scanOrders)
            require(value == "point_major", "scan_order attribute must be point_major");

        std::cout << "YALK point-major contract passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
