#include "orbita_stand/config.h"
#include "orbita_stand/ubsi_procedures.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef KTMA_SOURCE_DIR
#define KTMA_SOURCE_DIR "."
#endif

using namespace orbita::stand;

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

bool hasResourceRequirement(
    const ScenarioNode& node,
    const std::string& resource,
    const std::string& capability)
{
    return std::any_of(
        node.requiredResources.begin(), node.requiredResources.end(),
        [&](const ResourceRequirement& requirement) {
            return requirement.resource == resource
                && requirement.capability == capability;
        });
}

void verifyNode(const ScenarioNode& node)
{
    require(node.title.find("\xEF\xBF\xBD") == std::string::npos,
        "Production stage title contains a UTF-8 replacement character");
    require(node.title.find(u8"Рџ") == std::string::npos
            && node.title.find(u8"С‚") == std::string::npos,
        "Production stage title contains UTF-8/CP1251 mojibake");
    for (const auto& capability : node.requiredCapabilities) {
        require(capability != "orbita.parameter_source",
            "Production scenario must not require legacy Orbita/E20");
    }
    for (const auto& requirement : node.requiredResources) {
        require(requirement.capability != "orbita.parameter_source",
            "Production scenario resource must not require legacy Orbita/E20");
    }
    if (node.procedure == "ubsi.yvp") {
        const auto count = node.arguments.find("channel_count");
        require(count != node.arguments.end() && count->second == "8",
            "YVP V7/ISD scenario must parse channel_count=8");
        require(node.arguments.find("yvp_cell") == node.arguments.end()
                && node.arguments.find("yalk_addresses") == node.arguments.end(),
            "YVP V7/ISD scenario must not depend on adapter/YALK addressing");
        require(hasResourceRequirement(
                    node, "measure.reference", "measure.reference_ac_voltage")
                && hasResourceRequirement(
                    node, "measure.reference", "measure.reference_frequency")
                && hasResourceRequirement(
                    node, "switch_matrix.primary", "stand.switch_matrix"),
            "YVP V7/ISD scenario must require V7 and ISD delivery resources");
    }
    for (const auto& child : node.children) verifyNode(child);
}

} // namespace

int main()
{
    try {
        ScenarioEngine engine;
        registerUbsiProcedures(engine);

        const std::vector<std::string> files = {
            "data/scenarios/ubsi_production_full.yaml",
            "data/scenarios/ubsi_production_power.yaml",
            "data/scenarios/ubsi_production_yalk.yaml",
            "data/scenarios/ubsi_production_ytp.yaml",
            "data/scenarios/ubsi_production_yvp.yaml"};

        for (const auto& relative : files) {
            const auto scenario = loadScenarioYaml(
                std::string(KTMA_SOURCE_DIR) + "/" + relative);
            require(scenario.id.rfind("ktma.ubsi.production.", 0) == 0,
                "Production scenario id is outside KTMA/UBSI namespace: " + relative);
            require(scenario.publicationState == PublicationState::Published,
                "Production scenario must be published: " + relative);
            require(scenario.title.find("\xEF\xBF\xBD") == std::string::npos,
                "Production scenario title contains a UTF-8 replacement character: " + relative);
            require(scenario.title.find(u8"Рџ") == std::string::npos
                    && scenario.title.find(u8"С‚") == std::string::npos,
                "Production scenario title contains UTF-8/CP1251 mojibake: " + relative);
            const auto errors = engine.validate(scenario);
            if (!errors.empty()) {
                std::string message = "Production scenario validation failed: " + relative;
                for (const auto& error : errors) message += "\n - " + error;
                throw std::runtime_error(message);
            }
            for (const auto& node : scenario.steps) verifyNode(node);
        }

        std::cout << "KTMA UBSI production scenarios OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
