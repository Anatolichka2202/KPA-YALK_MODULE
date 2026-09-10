#include "ktma/ubsi/production.h"
#include "ktma/ubsi/production_ledger.h"
#include "registrar.h"
#include "orbita_stand/run_store.h"

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <array>
#include <chrono>
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
    report.product = {"p1", "UBSI", "РЈР‘РЎР-0001"};
    report.components = {
        component("c-yalk", "YALK-96", "РЇР›Рљ-001"),
        component("c-ytp", "YTP", "РЇРўРџ-001"),
        component("c-yvp", "YVP", "РЇР’Рџ-001"),
        component("c-power", "YP-P", "РЇРџРџ-001")};
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
    require(full.productSerial == "РЈР‘РЎР-0001", "product serial lost");
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

bool databaseHasTable(const QString& path, const QString& table, const QString& connectionName)
{
    bool found = false;
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        require(database.open(), "cannot inspect lifecycle database");
        QSqlQuery query(database);
        query.prepare(QStringLiteral(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?"));
        query.addBindValue(table);
        require(query.exec(), "cannot query lifecycle database schema");
        found = query.next();
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return found;
}

void persistedLifecycleContract()
{
    QTemporaryDir directory;
    require(directory.isValid(), "temporary lifecycle directory unavailable");
    const QString registrarPath = directory.filePath(QStringLiteral("registrar.db"));
    const QString runsPath = directory.filePath(QStringLiteral("runs/runs.db"));
    require(QDir().mkpath(directory.filePath(QStringLiteral("runs"))),
        "cannot create isolated runs directory");

    std::string productId;
    std::string productionId;
    const std::string scenarioRunId = "fake-production-run-1";
    {
        registrar::Registrar registrar(registrarPath.toStdString());
        productId = registrar.createProduct("UBSI", "UBSI-LIFECYCLE-001");
        for (const auto& item : std::array<std::pair<const char*, const char*>, 4>{{
                 {"YALK-96", "YALK-LIFE-001"}, {"YTP", "YTP-LIFE-001"},
                 {"YVP", "YVP-LIFE-001"}, {"YP-P", "YPP-LIFE-001"}}}) {
            const auto componentId = registrar.createComponent(item.first, item.second);
            registrar.installComponent(productId, componentId);
        }

        const auto context = ubsi::buildProductionRunContext(
            registrar.productReport(productId), registrar::Stage::Primary,
            ubsi::ProductionPackage::Yalk);
        ubsi::ProductionLedger ledger(registrarPath.toStdString());
        productionId = ledger.begin(context);

        orbita::stand::ScenarioRunResult run;
        run.runId = scenarioRunId;
        run.scenarioId = "ktma.ubsi.production.yalk";
        run.scenarioVersion = "1.0.0";
        run.catalogVersion = "2026.08.29-yalk-1";
        run.profileVersion = "fake-profile";
        run.objectSerial = "UBSI-LIFECYCLE-001";
        run.startedAt = std::chrono::system_clock::now();
        run.finishedAt = run.startedAt + std::chrono::seconds(1);
        run.verdict = orbita::stand::RunVerdict::Ok;
        orbita::stand::StepRunResult step;
        step.nodeId = "yalk_channels";
        step.title = "YALK channels";
        step.verdict = orbita::stand::RunVerdict::Ok;
        run.steps.push_back(std::move(step));
        orbita::stand::RunStore store(runsPath.toStdString());
        store.save(run);

        ledger.attachRun(productionId, scenarioRunId);
        ledger.finish(productionId, ubsi::ProductionRunStatus::Norm);
    }

    {
        registrar::Registrar reopenedRegistrar(registrarPath.toStdString());
        const auto product = reopenedRegistrar.findProductBySerial("UBSI-LIFECYCLE-001");
        require(product && product->id == productId,
            "registered UBSI did not survive database reopen");
        require(reopenedRegistrar.listInstalledComponents(productId).size() == 4,
            "four-cell composition did not survive database reopen");
        ubsi::ProductionLedger reopenedLedger(registrarPath.toStdString());
        const auto history = reopenedLedger.listForProduct(productId);
        require(history.size() == 1 && history.front().id == productionId,
            "production history did not survive database reopen");
        require(history.front().runId == scenarioRunId
                && history.front().status == ubsi::ProductionRunStatus::Norm,
            "production verdict/run link did not survive database reopen");
    }

    require(databaseHasTable(registrarPath, QStringLiteral("products"),
                             QStringLiteral("inspect_registrar_products")),
        "registrar.db is missing product lifecycle tables");
    require(databaseHasTable(registrarPath, QStringLiteral("ubsi_production_runs"),
                             QStringLiteral("inspect_registrar_production")),
        "registrar.db is missing production lifecycle tables");
    require(!databaseHasTable(registrarPath, QStringLiteral("test_runs"),
                              QStringLiteral("inspect_registrar_runs")),
        "raw run storage leaked into registrar.db");
    require(databaseHasTable(runsPath, QStringLiteral("test_runs"),
                             QStringLiteral("inspect_runs")),
        "runs.db is missing the persisted fake run");
    require(!databaseHasTable(runsPath, QStringLiteral("products"),
                              QStringLiteral("inspect_runs_products")),
        "product lifecycle leaked into runs.db");
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

    const auto readme = readFile("deliveries/ktma/ubsi/README.md");
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
        persistedLifecycleContract();
        separationContract();
        std::cout << "KTMA UBSI production contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

