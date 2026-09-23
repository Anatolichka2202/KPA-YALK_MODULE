#include "orbita_stand/config.h"
#include "orbita_stand/project.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace orbita::stand;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::string filename(const std::string& path)
{
    return std::filesystem::u8path(path).filename().string();
}

} // namespace

int main()
{
    try {
        const std::filesystem::path sourceRoot = ORBITA_SOURCE_DIR;
        const auto project = loadProjectPackage(
            (sourceRoot / "projects/ktma/project.yaml").string());

        require(project.id == "ktma", "KTMA project id mismatch");
        require(project.version == "1.0.0", "KTMA project version mismatch");
        require(project.dutTypes.size() == 1
                && project.dutTypes.front() == "UBSI_468157_002",
            "KTMA project must expose UBSI as the current DUT type");
        require(project.workflows.size() == 5,
            "KTMA V1 package must expose free, TU normal/climate and production normal/climate");

        const auto* free = findWorkflow(project, "free");
        require(free && free->allowDynamicScenario && free->scenarioPath.empty(),
            "Free workflow must permit a dynamic unregistered scenario");
        require(!free->registration.required && !free->registration.attachIfFound,
            "Free workflow must stay outside the production registrar lifecycle");

        const auto* tu = findWorkflow(project, "tu_normal");
        require(tu && filename(tu->scenarioPath) == "ubsi_ulk_combined_check.yaml",
            "TU normal workflow must select the canonical full UBSI TU scenario");
        require(!tu->registration.required && tu->registration.attachIfFound,
            "TU workflow must run without registration but attach an existing DUT when found");

        const auto* production = findWorkflow(project, "production");
        require(production && filename(production->scenarioPath) == "ubsi_production_full.yaml",
            "Production workflow must select the full production scenario");
        require(production->registration.required,
            "Production workflow must require a registered DUT");
        require(production->referenceWorkflow == "tu_normal",
            "Production workflow must retain TU normal as its formal reference");

        const auto* tuClimate = findWorkflow(project, "tu_climate");
        const auto* productionClimate = findWorkflow(project, "production_climate");
        require(tuClimate && filename(tuClimate->environmentPath) == "climate.yaml",
            "TU climate workflow must declare a climate environment profile");
        require(productionClimate
                && productionClimate->referenceWorkflow == "tu_climate",
            "Climate production workflow must reference the climate TU workflow");

        const auto profile = loadStandProfile(project.equipmentProfilePath);
        require(profile.id == "ktma-main", "KTMA project must compose the verified stand profile");
        require(findComponentByBinding(profile, "power.dut") != nullptr,
            "KTMA project profile must bind power.dut");
        require(findComponentByBinding(profile, "measure.reference") != nullptr,
            "KTMA project profile must bind measure.reference");
        require(findComponentByBinding(profile, "switch_matrix.primary") != nullptr,
            "KTMA project profile must bind switch_matrix.primary");

        std::cout << "Project package contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Project package contract failed: " << error.what() << '\n';
        return 1;
    }
}
