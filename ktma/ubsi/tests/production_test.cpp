#include "ktma/ubsi/production.h"
#include "ktma/ubsi/production_ledger.h"

#include <QTemporaryDir>

#include <fstream>
#include <iostream>
#include <QCoreApplication>
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
    require(full.scenarioCode == "PROD_FULL", "full package must use Production scenario");
    for (const auto& item : full.composition)
        require(item.affected, "full UBSI run must affect every installed cell");

    const auto yalk = ubsi::buildProductionRunContext(
        report, registrar::Stage::Primary, ubsi::ProductionPackage::Yalk);
    require(yalk.scenarioCode == "PROD_YALK", "YALK must use Production scenario");
    require(affected(yalk, "YALK-96"), "YALK package must affect YALK");

    const auto ytp = ubsi::buildProductionRunContext(
        report, registrar::Stage::Primary, ubsi::ProductionPackage::Ytp);
    require(ytp.scenarioCode == "PROD_YTP", "YTP must use Production scenario");
    require(affected(ytp, "YTP"), "YTP package must affect YTP");

    const auto yvp = ubsi::buildProductionRunContext(
        report, registrar::Stage::Primary, ubsi::ProductionPackage::Yvp);
    require(yvp.scenarioCode == "PROD_YVP", "YVP must use Production scenario");
    require(affected(yvp, "YVP"), "YVP package must affect YVP");
    require(affected(yvp, "YALK-96"), "YVP package must include linked YALK 88..96 path");
    require(!affected(yvp, "YTP") && !affected(yvp, "YP-P"),
        "YVP package must not claim unrelated cells");

    const auto power = ubsi::buildProductionRunContext(
        report, registrar::Stage::ClimateNormal, ubsi::ProductionPackage::PowerConsumption);
    require(power.scenarioCode == "PROD_POWER", "power must use Production scenario");
    require(affected(power, "YP-P"), "power package must affect YP-P");
    require(!affected(power, "YALK-96") && !affected(power, "YTP") && !affected(power, "YVP"),
        "power package affected set is wrong");

    require(ubsi::productionPackageFromCode("YALK_FULL_5_6") == ubsi::ProductionPackage::Yalk,
        "legacy Qt YALK code must resolve to Production YALK package");
    require(ubsi::productionPackageFromCode("YTP_FULL_5_6") == ubsi::ProductionPackage::Ytp,
        "legacy Qt YTP code must resolve to Production YTP package");
    require(ubsi::productionPackageFromCode("ULK_COMBINED_CHECK") == ubsi::ProductionPackage::FullUbsi,
        "legacy Qt combined code must resolve to full Production package");
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

void ledgerContract()
{
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory unavailable");
    ubsi::ProductionLedger ledger(
        directory.filePath(QStringLiteral("registrar.db")).toStdString());

    const auto context = ubsi::buildProductionRunContext(
        completeProduct(), registrar::Stage::PottingClimatePlus,
        ubsi::ProductionPackage::Yvp);
    const auto id = ledger.begin(context);
    auto record = ledger.get(id);
    require(record.status == ubsi::ProductionRunStatus::InProgress,
        "new production run must be IN_PROGRESS");
    require(record.context.composition.size() == 4,
        "ledger must persist complete composition snapshot");
    require(affected(record.context, "YVP") && affected(record.context, "YALK-96"),
        "ledger lost affected YVP/YALK cells");

    ledger.attachRun(id, "scenario-run-1");
    ledger.finish(id, ubsi::ProductionRunStatus::StandError);
    record = ledger.get(id);
    require(record.runId == "scenario-run-1", "ScenarioEngine run_id not persisted");
    require(record.status == ubsi::ProductionRunStatus::StandError,
        "STAND_ERROR must not collapse to Cancelled/Incomplete");
    require(!record.finishedAt.empty(), "finished timestamp missing");

    const auto history = ledger.listForProduct("p1");
    require(history.size() == 1 && history.front().id == id,
        "product production history is incomplete");
}

void separationContract()
{
    const char* productionFiles[] = {
        "data/scenarios/ubsi_production_full.yaml",
        "data/scenarios/ubsi_production_power.yaml",
        "data/scenarios/ubsi_production_yalk.yaml",
        "data/scenarios/ubsi_production_ytp.yaml",
        "data/scenarios/ubsi_production_yvp.yaml"};
    for (const auto* file : productionFiles) {
        const auto scenario = readFile(file);
        require(scenario.find("id: ktma.ubsi.production.") != std::string::npos,
            std::string("not a dedicated Production scenario: ") + file);
        require(scenario.find("orbita.parameter_source") == std::string::npos,
            std::string("Production scenario depends on Orbita/E20: ") + file);
    }

    const auto yvpScenario = readFile("data/scenarios/ubsi_production_yvp.yaml");
    require(yvpScenario.find("yalk_address_min: 88") != std::string::npos
        && yvpScenario.find("yalk_address_max: 96") != std::string::npos,
        "YVP production scenario must lock corrected YALK range 88..96");

    const auto catalog = readFile("data/catalog/catalog.yaml");
    const auto yvp = catalog.find("parameter_group: yvp_fast", catalog.find("bindings:"));
    require(yvp != std::string::npos, "YVP binding missing");
    const auto yvpEnd = catalog.find("\ninstances:", yvp);
    const auto yvpBlock = catalog.substr(yvp, yvpEnd - yvp);
    require(yvpBlock.find("source: ulk.parameter_source") != std::string::npos,
        "YVP production data must come from YALK/ULK");
    require(yvpBlock.find("orbita.parameter_source") == std::string::npos,
        "YVP production binding must not return to Orbita/E20");
    require(yvpBlock.find("confirmed: false") != std::string::npos,
        "stale eight-address YVP binding must remain fail-safe until 88..96 mapping is commissioned");

    const auto rootCmake = readFile("CMakeLists.txt");
    require(rootCmake.find("project(MilTechStation") != std::string::npos,
        "root build identity must be MilTech Station, not OrbitaSystem");
    require(rootCmake.find("add_subdirectory(orbita)") != std::string::npos,
        "Orbita subsystem must remain available for future BSI/RPU product deliveries");

    const auto readme = readFile("ktma/ubsi/README.md");
    require(readme.find("BSI, RPU") != std::string::npos,
        "UBSI boundary documentation must preserve Orbita future-product role");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        compositionContract();
        mandatoryCompositionContract();
        ledgerContract();
        separationContract();
        std::cout << "KTMA UBSI production contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
