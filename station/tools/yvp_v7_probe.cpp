#include "orbita_stand/config.h"
#include "orbita_stand/equipment_runtime.h"
#include "orbita_stand/ubsi_procedures.h"

#include <QCoreApplication>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

void configureYvpNode(orbita::stand::ScenarioNode& node,
                      const std::string& channels,
                      const std::string& gains,
                      const std::string& frequencies)
{
    if (node.procedure == "ubsi.yvp" || node.procedure == "yvp.v7_isd") {
        if (!channels.empty()) node.arguments["commissioning_channels"] = channels;
        if (!gains.empty()) node.arguments["gains_mv_per_pcl"] = gains;
        if (!frequencies.empty()) node.arguments["frequencies_hz"] = frequencies;
    }
    for (auto& child : node.children)
        configureYvpNode(child, channels, gains, frequencies);
}

void printStep(const orbita::stand::StepRunResult& step, unsigned depth = 0)
{
    const std::string indent(depth * 2, ' ');
    std::cout << indent << "STEP " << step.nodeId << ' '
              << orbita::stand::toString(step.verdict) << ' ' << step.message << '\n';
    for (const auto& measurement : step.measurements) {
        std::cout << indent << "  POINT " << measurement.parameterKey
                  << " measured=" << measurement.measured << ' ' << measurement.unit;
        for (const auto& key : {"yvp_channel", "gain_mv_per_pc", "set_frequency_hz",
                                "rigol_input_vpp", "v7_output_vrms",
                                "measured_frequency_hz"}) {
            const auto found = measurement.attributes.find(key);
            if (found != measurement.attributes.end())
                std::cout << ' ' << key << '=' << found->second;
        }
        std::cout << '\n';
    }
    for (const auto& child : step.children) printStep(child, depth + 1);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc < 4 || argc > 7) {
        std::cerr << "Usage: orbita_yvp_v7_probe <stand-profile.yaml> <plugin-directory> "
                     "<scenario.yaml> [channels] [gains] [frequencies]\n"
                     "Example: ... ubsi_production_yvp.yaml 1 1 20\n";
        return EXIT_FAILURE;
    }

    try {
        auto profile = orbita::stand::loadStandProfile(argv[1]);
        auto scenario = orbita::stand::loadScenarioYaml(argv[3]);
        const std::string channels = argc > 4 ? argv[4] : "";
        const std::string gains = argc > 5 ? argv[5] : "";
        const std::string frequencies = argc > 6 ? argv[6] : "";
        for (auto& step : scenario.steps)
            configureYvpNode(step, channels, gains, frequencies);

        orbita::stand::EquipmentPluginManager manager;
        manager.loadDirectory(argv[2]);
        orbita::stand::EquipmentRegistry registry;
        std::vector<std::shared_ptr<orbita::stand::EquipmentDevice>> devices;
        orbita::stand::instantiateProfile(profile, manager, registry, devices);

        orbita::stand::ScenarioEngine engine;
        orbita::stand::registerUbsiProcedures(engine);
        const auto errors = engine.validate(scenario);
        if (!errors.empty()) {
            for (const auto& error : errors) std::cerr << "VALIDATION " << error << '\n';
            registry.safeStopAll();
            return 2;
        }

        const auto result = engine.run(scenario, registry, profile.version,
            "YVP-COMMISSIONING", false,
            [](const orbita::stand::RunEvent& event) {
                std::cout << "EVENT " << event.nodeId << ' ' << event.stage << ' '
                          << orbita::stand::toString(event.verdict) << ' '
                          << event.message << '\n';
            });
        for (const auto& step : result.steps) printStep(step);
        std::cout << "RESULT " << orbita::stand::toString(result.verdict)
                  << " run_id=" << result.runId << '\n';
        return result.verdict == orbita::stand::RunVerdict::Error
                || result.verdict == orbita::stand::RunVerdict::Aborted ? 3 : 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
