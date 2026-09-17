#include "orbita_stand/config.h"
#include "orbita_stand/ubsi_procedures.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <set>
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

bool componentProvides(
    const ComponentProfile& component,
    const std::string& capability)
{
    const auto& capabilities = component.capabilities.empty()
        ? component.bindings
        : component.capabilities;
    return std::find(capabilities.begin(), capabilities.end(), capability)
        != capabilities.end();
}

bool isPhysicalCapability(const std::string& capability)
{
    static const std::set<std::string> physical = {
        "power.dc_supply",
        "ulk.parameter_source",
        "stand.switch_matrix",
        "measure.reference_voltage",
        "measure.dc_current",
        "measure.reference_ac_voltage",
        "measure.reference_frequency",
        "signal.generator",
        "measure.waveform",
    };
    return physical.count(capability) != 0;
}

void verifyNode(const ScenarioNode& node, const StandProfile& profile)
{
    require(node.title.find("\xEF\xBF\xBD") == std::string::npos,
        "Scenario stage title contains a UTF-8 replacement character");
    require(node.title.find(u8"Рџ") == std::string::npos
            && node.title.find(u8"С‚") == std::string::npos,
        "Scenario stage title contains UTF-8/CP1251 mojibake");
    for (const auto& capability : node.requiredCapabilities) {
        require(capability != "orbita.parameter_source",
            "KTMA acceptance scenario must not require legacy Orbita/E20");
        if (isPhysicalCapability(capability)) {
            const bool routed = std::any_of(
                node.requiredResources.begin(), node.requiredResources.end(),
                [&](const ResourceRequirement& requirement) {
                    return requirement.capability == capability;
                });
            require(routed,
                "Physical capability requirement has no delivery resource: "
                    + capability + " in step " + node.id);
        }
    }
    for (const auto& requirement : node.requiredResources) {
        require(requirement.capability != "orbita.parameter_source",
            "KTMA acceptance scenario resource must not require legacy Orbita/E20");

        const auto* component = findComponentByBinding(profile, requirement.resource);
        require(component != nullptr,
            "Scenario resource is not declared by KTMA profile: "
                + requirement.resource);
        require(component->kind == "equipment",
            "Physical scenario resource must resolve to equipment: "
                + requirement.resource);
        require(componentProvides(*component, requirement.capability),
            "KTMA resource " + requirement.resource
                + " does not provide required capability " + requirement.capability);
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
    for (const auto& child : node.children) verifyNode(child, profile);
}

void verifyScenarioContracts(
    const ScenarioDefinition& scenario,
    const StandProfile& profile,
    ScenarioEngine& engine,
    const std::string& label)
{
    const auto errors = engine.validate(scenario);
    if (!errors.empty()) {
        std::string message = label + " scenario validation failed";
        for (const auto& error : errors) message += "\n - " + error;
        throw std::runtime_error(message);
    }
    for (const auto& node : scenario.steps) verifyNode(node, profile);
}

} // namespace

int main()
{
    try {
        ScenarioEngine engine;
        registerUbsiProcedures(engine);

        const auto profile = loadStandProfile(
            std::string(KTMA_SOURCE_DIR) + "/data/profiles/stand_ktma.yaml");

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
            verifyScenarioContracts(scenario, profile, engine, "Production");
        }

        const auto canonicalTu = loadScenarioYaml(
            std::string(KTMA_SOURCE_DIR) + "/data/scenarios/ubsi_ulk_combined_check.yaml");
        require(canonicalTu.publicationState == PublicationState::Published,
            "Canonical TU scenario must be published");
        verifyScenarioContracts(canonicalTu, profile, engine, "Canonical TU");

        const auto yalkTu = loadScenarioYaml(
            std::string(KTMA_SOURCE_DIR) + "/data/scenarios/ubsi_yalk_tu_5_6.yaml");
        require(yalkTu.publicationState == PublicationState::Published,
            "Standalone YALK TU scenario must be published");
        verifyScenarioContracts(yalkTu, profile, engine, "Standalone YALK TU");

        const auto ytpTu = loadScenarioYaml(
            std::string(KTMA_SOURCE_DIR) + "/data/scenarios/ubsi_ytp_tu_5_6.yaml");
        require(ytpTu.publicationState == PublicationState::Published,
            "Standalone YTP TU scenario must be published");
        verifyScenarioContracts(ytpTu, profile, engine, "Standalone YTP TU");

        const auto ytp120 = loadScenarioYaml(
            std::string(KTMA_SOURCE_DIR) + "/data/scenarios/ubsi_ytp_120_check.yaml");
        require(ytp120.publicationState == PublicationState::Published,
            "Fixed-120 YTP diagnostic must be published");
        verifyScenarioContracts(ytp120, profile, engine, "Fixed-120 YTP");

        const auto contactThresholds = loadScenarioYaml(
            std::string(KTMA_SOURCE_DIR) + "/data/scenarios/ubsi_yalk_contact_thresholds.yaml");
        require(contactThresholds.publicationState == PublicationState::Published,
            "Optional YALK contact-threshold scenario must be published");
        verifyScenarioContracts(
            contactThresholds, profile, engine, "Optional YALK contact thresholds");

        const auto legacyTrace = loadScenarioYaml(
            std::string(KTMA_SOURCE_DIR) + "/data/scenarios/ubsi_tu_5_6.yaml");
        require(legacyTrace.publicationState == PublicationState::Draft,
            "Legacy trace scenario must remain a draft");
        verifyScenarioContracts(legacyTrace, profile, engine, "Legacy TU trace");

        std::cout << "KTMA UBSI production/TU resource contracts OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
