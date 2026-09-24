#include "orbita_stand/config.h"

#include <QCoreApplication>
#include <QDir>

#include <algorithm>
#include <iostream>
#include <stdexcept>

#ifndef ORBITA_SOURCE_DIR
#define ORBITA_SOURCE_DIR "."
#endif

using namespace orbita::stand;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

bool contains(const std::vector<std::string>& values, const std::string& value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool requiresResource(
    const ScenarioNode& node,
    const std::string& resource,
    const std::string& capability)
{
    return std::any_of(node.requiredResources.begin(), node.requiredResources.end(),
        [&](const ResourceRequirement& requirement) {
            return requirement.resource == resource && requirement.capability == capability;
        });
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const QDir sourceRoot(QString::fromUtf8(ORBITA_SOURCE_DIR));
        const auto profilePath = sourceRoot.filePath(QStringLiteral("data/profiles/stand_ktma.yaml"));
        const auto profile = loadStandProfile(profilePath.toStdString());

        const auto* sampleSource = findComponentByBinding(
            profile, "telemetry.orbita.sample_source");
        require(sampleSource != nullptr, "Orbita sample source is not declared by KTMA delivery");
        require(sampleSource->kind == "sample_source", "Wrong component kind for Orbita input");
        require(sampleSource->provider == "miltech.sample.e2010", "Wrong E2010 provider id");
        require(sampleSource->configuration.at("channel") == "1", "E2010 channel was not loaded");
        require(sampleSource->configuration.at("sample_rate_khz") == "10000",
                "E2010 sample rate was not loaded");
        require(sampleSource->capabilities.empty(),
                "Non-equipment sample source must not expose equipment capabilities");

        const auto* supplyComponent = findComponentByBinding(profile, "power.dut");
        require(supplyComponent != nullptr, "Power supply logical role is not declared by KTMA delivery");
        require(supplyComponent->id == "dc-supply", "Power role resolved to the wrong component");
        require(supplyComponent->kind == "equipment", "Power supply must be an equipment component");
        require(supplyComponent->provider == "orbita.akip_1160_pair",
                "Equipment provider was not preserved in canonical component model");
        require(contains(supplyComponent->capabilities, "power.dc_supply"),
                "Power role lost its equipment capability contract");
        require(!contains(supplyComponent->bindings, "power.dc_supply"),
                "Canonical equipment bind must contain roles, not capabilities");

        const auto* reference = findComponentByBinding(profile, "measure.reference");
        require(reference != nullptr && reference->id == "v7-reference",
                "Reference-measurement role did not resolve to V7 component");
        require(contains(reference->capabilities, "measure.reference_voltage")
                    && contains(reference->capabilities, "measure.dc_current")
                    && contains(reference->capabilities, "measure.reference_ac_voltage")
                    && contains(reference->capabilities, "measure.reference_frequency"),
                "Reference role lost V7 capability contracts");
        require(reference->configuration.at("timeout_ms") == "25000",
                "UBSI production V7 timeout must preserve the proven 25 s YVP margin");
        require(reference->configuration.at("voltage_command") == "MEAS:VOLT:DC?",
                "UBSI production V7 DC command must match the proven donor tract");
        require(reference->configuration.at("ac_voltage_command") == "MEAS:VOLT:AC?",
                "UBSI production V7 AC command must match the proven donor tract");

        const auto legacyView = std::find_if(profile.devices.begin(), profile.devices.end(),
            [](const DeviceProfile& device) { return device.id == "dc-supply"; });
        require(legacyView != profile.devices.end(),
                "Canonical equipment component was not projected to the legacy DeviceProfile view");
        require(legacyView->pluginId == "orbita.akip_1160_pair",
                "Derived DeviceProfile lost the equipment provider id");
        require(contains(legacyView->bindCapabilities, "power.dc_supply"),
                "Derived DeviceProfile lost the explicit equipment capability");
        require(!contains(legacyView->bindCapabilities, "power.dut"),
                "Logical resource role leaked into legacy capability routing");

        require(profile.components.size() == 7,
                "KTMA delivery must declare one sample source and six equipment components");
        require(profile.devices.size() == 6,
                "Legacy equipment view must be derived for all six KTMA equipment components");

        const auto powerScenario = loadScenarioYaml(sourceRoot.filePath(
            QStringLiteral("data/scenarios/ubsi_production_power.yaml")).toStdString());
        require(powerScenario.steps.size() == 2,
                "Production power scenario must contain measurement and safe-off steps");
        const auto& supplyRange = powerScenario.steps.front();
        require(supplyRange.requiredCapabilities.empty(),
                "Production power measurement must no longer depend on global capability routing");
        require(requiresResource(supplyRange, "power.dut", "power.dc_supply"),
                "Production power measurement must select the DUT supply role");
        require(requiresResource(supplyRange, "dut.parameter_source", "ulk.parameter_source"),
                "Production power measurement must select the DUT telemetry role");
        const auto& safeOff = powerScenario.steps.back();
        require(safeOff.requiredCapabilities.empty(),
                "Production power safe-off must no longer depend on global capability routing");
        require(requiresResource(safeOff, "power.dut", "power.dc_supply"),
                "Production power safe-off must select the DUT supply role");

        const auto yvpScenario = loadScenarioYaml(sourceRoot.filePath(
            QStringLiteral("data/scenarios/ubsi_production_yvp.yaml")).toStdString());
        require(yvpScenario.steps.size() == 4,
                "Production YVP scenario must keep ISD baseline, power, physical YVP and safe-off steps");
        require(yvpScenario.steps.front().procedure == "ubsi.isd_baseline",
                "Standalone production YVP must establish the same ISD baseline as YALK/YTP/full runs");
        require(requiresResource(yvpScenario.steps.front(),
                    "switch_matrix.primary", "stand.switch_matrix"),
                "YVP ISD baseline must select the switch-matrix resource explicitly");
        const auto& yvp = yvpScenario.steps.at(2);
        require(yvp.procedure == "ubsi.yvp",
                "Production YVP scenario must use the physical V7/ISD procedure");
        require(yvp.arguments.at("mapping_confirmed") == "true"
                    && yvp.arguments.at("active_outputs_confirmed") == "true",
                "Production YVP scenario must explicitly authorize only the confirmed physical map");
        require(yvp.arguments.at("input_1_contacts") == "33"
                    && yvp.arguments.at("measurement_1_contacts") == "44"
                    && yvp.arguments.at("channel_1_gain_contacts") == "1,2,3,4",
                "Production YVP channel 1 map drifted from the confirmed donor tract");
        require(yvp.arguments.at("input_8_contacts") == "40"
                    && yvp.arguments.at("measurement_8_contacts") == "73"
                    && yvp.arguments.at("channel_8_gain_contacts") == "29,30,31,32",
                "Production YVP channel 8 map drifted from the confirmed donor tract");
        require(yvp.arguments.find("verdict_policy") == yvp.arguments.end(),
                "Production YVP must not import donor manual-confirmed acceptance overrides");

        std::cout << "component profile contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "component profile contract failed: " << error.what() << '\n';
        return 1;
    }
}
