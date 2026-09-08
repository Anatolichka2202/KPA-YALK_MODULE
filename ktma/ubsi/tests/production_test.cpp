#include "ktma/ubsi/production.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef KTMA_SOURCE_DIR
#define KTMA_SOURCE_DIR "."
#endif

using namespace ktma;

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

registrar::ComponentBinding component(
    std::string id, std::string type, std::string serial, bool active = true)
{
    registrar::ComponentBinding value;
    value.productId = "p1";
    value.componentId = std::move(id);
    value.componentType = std::move(type);
    value.serialNumber = std::move(serial);
    value.active = active;
    return value;
}

registrar::ProductReport completeProduct()
{
    registrar::ProductReport report;
    report.product = {"p1", "UBSI", "УБСИ-0001"};
    report.components = {
        component("c-yalk", "YALK-96", "ЯЛК-001"),
        component("c-ytp", "YTP", "ЯТП-001"),
        component("c-yvp", "YVP", "ЯВП-001"),
        component("c-power", "YP-P", "ЯПП-001")};
    return report;
}

bool affected(const ubsi::ProductionRunContext& context, const std::string& type)
{
    for (const auto& component : context.composition) {
        if (component.componentType == type) return component.affected;
    }
    throw std::runtime_error("missing component in snapshot: " + type);
}

std::string readFile(const char* relative)
{
    const std::string path = std::string(KTMA_SOURCE_DIR) + "/" + relative;
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path);
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

void compositionContract()
{
    const auto report = completeProduct();
    const auto full = ubsi::buildProductionRunContext(
        report, registrar::Stage::ClimatePlus, ubsi::ProductionPackage::FullUbsi);
    require(full.productSerial == "УБСИ-0001", "product serial lost");
    require(full.composition.size() == 4, "production snapshot must contain four active cells");
    require(full.scenarioCode == "ULK_COMBINED_CHECK", "full package scenario mismatch");
    for (const auto& item : full.composition)
        require(item.affected, "full UBSI run must affect every installed cell");

    const auto yvp = ubsi::buildProductionRunContext(
        report, registrar::Stage::Primary, ubsi::ProductionPackage::Yvp);
    require(affected(yvp, "YVP"), "YVP package must affect YVP");
    require(affected(yvp, "YALK-96"), "YVP package must include linked YALK 89..96 path");
    require(!affected(yvp, "YTP") && !affected(yvp, "YP-P"),
        "YVP package must not claim unrelated cells");

    const auto power = ubsi::buildProductionRunContext(
        report, registrar::Stage::ClimateNormal, ubsi::ProductionPackage::PowerConsumption);
    require(affected(power, "YP-P"), "power package must affect YP-P");
    require(!affected(power, "YALK-96") && !affected(power, "YTP") && !affected(power, "YVP"),
        "power package affected set is wrong");
}

void mandatoryCompositionContract()
{
    auto report = completeProduct();
    report.components.pop_back();
    bool rejected = false;
    try {
        (void)ubsi::buildProductionRunContext(
            report, registrar::Stage::Primary, ubsi::ProductionPackage::Ytp);
    } catch (const std::logic_error&) {
        rejected = true;
    }
    require(rejected,
        "even a component package must be blocked until the four-cell UBSI composition exists");
}

void separationContract()
{
    const auto combined = readFile("data/scenarios/ubsi_ulk_combined_check.yaml");
    const auto catalog = readFile("data/catalog/catalog.yaml");
    require(combined.find("orbita.parameter_source") == std::string::npos,
        "current UBSI scenario must not depend on Orbita/E20 parameter source");

    const auto yvp = catalog.find("parameter_group: yvp_fast", catalog.find("bindings:"));
    require(yvp != std::string::npos, "YVP binding missing");
    const auto yvpEnd = catalog.find("\ninstances:", yvp);
    const auto yvpBlock = catalog.substr(yvp, yvpEnd - yvp);
    require(yvpBlock.find("source: ulk.parameter_source") != std::string::npos,
        "YVP production data must come from YALK/ULK");
    require(yvpBlock.find("orbita.parameter_source") == std::string::npos,
        "YVP production binding must not return to Orbita/E20");

    const auto rootCmake = readFile("CMakeLists.txt");
    require(rootCmake.find("project(MilTechStation") != std::string::npos,
        "root build identity must be MilTech Station, not OrbitaSystem");
}

} // namespace

int main()
{
    try {
        compositionContract();
        mandatoryCompositionContract();
        separationContract();
        std::cout << "KTMA UBSI production contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
