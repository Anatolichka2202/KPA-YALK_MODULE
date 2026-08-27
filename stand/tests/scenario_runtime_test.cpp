#include "orbita_stand/catalog.h"
#include "orbita_stand/config.h"
#include "orbita_stand/dho_waveform.h"
#include "orbita_stand/equipment_runtime.h"
#include "orbita_stand/report_writer.h"
#include "orbita_stand/run_store.h"
#include "orbita_stand/ubsi_procedures.h"
#include "orbita_stand/telemetry_procedures.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace orbita::stand;

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

class FakeEquipment final : public ICapabilityProvider {
public:
    bool hasCapability(const std::string& capability) const override
    {
        return capabilities.count(capability) != 0;
    }
    std::string invoke(const std::string& capability, const std::string& operation,
                       const std::map<std::string, std::string>&) override
    {
        if (capability == "orbita.parameter_source" && operation == "health") {
            return "status=ready\nframes_processed=12\nphrase_error_percent=0\n"
                   "group_error_percent=0\nchannel_count=8\n";
        }
        return "status=ready\n";
    }
    void safeStopAll() noexcept override { stopped = true; }
    std::set<std::string> capabilities;
    bool stopped = false;
};

ScenarioDefinition smallScenario()
{
    ScenarioDefinition scenario;
    scenario.id = "test";
    scenario.title = "Test";
    scenario.version = "1";
    scenario.catalogVersion = "1";
    scenario.publicationState = PublicationState::Published;
    scenario.steps = {
        {"ok", "OK", "1.1", "ok", {"a"}, {}, {}},
        {"fail", "FAIL", "1.2", "fail", {"a"}, {}, {}},
        {"last", "LAST", "1.3", "ok", {"a"}, {}, {}},
    };
    return scenario;
}

void engineSemantics()
{
    ScenarioEngine engine;
    engine.registerProcedure("ok", [](const ScenarioNode&, ProcedureContext&) {
        return ProcedureResult{RunVerdict::Ok, "ok", {}};
    });
    engine.registerProcedure("fail", [](const ScenarioNode&, ProcedureContext&) {
        return ProcedureResult{RunVerdict::Fail, "fail", {}};
    });
    FakeEquipment equipment;
    equipment.capabilities.insert("a");
    const auto run = engine.run(smallScenario(), equipment, "p1", "", false);
    require(run.verdict == RunVerdict::Fail, "FAIL must be the final verdict");
    require(run.steps.size() == 3, "Measurement FAIL must not stop later checks");
    require(equipment.stopped, "Equipment must be safely stopped after every run");

    equipment = {};
    const auto partial = engine.run(smallScenario(), equipment, "p1", "", true);
    require(partial.verdict == RunVerdict::Incomplete, "Missing capability must produce INCOMPLETE");
    require(partial.verdict != RunVerdict::Ok, "Partial run must never produce OK");

    ScenarioEngine errorEngine;
    errorEngine.registerProcedure("error", [](const ScenarioNode&, ProcedureContext&) {
        return ProcedureResult{RunVerdict::Error, "link lost", {}};
    });
    auto errorScenario = smallScenario();
    errorScenario.steps = {{"link", "Link", "1.1", "error", {"a"}, {}, {}}};
    equipment.capabilities.insert("a");
    const auto errorRun = errorEngine.run(errorScenario, equipment, "p1", "", false);
    require(errorRun.verdict == RunVerdict::Error, "Equipment error must stop the scenario with ERROR");

    ScenarioEngine abortEngine;
    abortEngine.registerProcedure("stop", [&abortEngine](const ScenarioNode&, ProcedureContext&) {
        abortEngine.requestStop();
        return ProcedureResult{RunVerdict::Ok, "first step", {}};
    });
    abortEngine.registerProcedure("ok", [](const ScenarioNode&, ProcedureContext&) {
        return ProcedureResult{RunVerdict::Ok, "second step", {}};
    });
    auto abortScenario = smallScenario();
    abortScenario.steps = {
        {"first", "First", "1.1", "stop", {"a"}, {}, {}},
        {"second", "Second", "1.2", "ok", {"a"}, {}, {}}};
    const auto abortedRun = abortEngine.run(abortScenario, equipment, "p1", "", false);
    require(abortedRun.verdict == RunVerdict::Aborted, "Operator stop must produce ABORTED");
    require(abortedRun.steps.size() == 2, "Abort must be recorded as the next step");
}

void configurationAndCatalog(const QString& root)
{
    const auto scenarioPath = QDir(root).filePath(QStringLiteral("data/scenarios/ubsi_tu_5_6.yaml"));
    const auto bsiDiagnosticPath = QDir(root).filePath(QStringLiteral("data/scenarios/bsi_diagnostic.yaml"));
    const auto profilePath = QDir(root).filePath(QStringLiteral("data/profiles/stand_ktma.yaml"));
    const auto catalogPath = QDir(root).filePath(QStringLiteral("data/catalog/catalog.yaml"));
    auto scenario = loadScenarioYaml(scenarioPath.toUtf8().toStdString());
    auto bsiDiagnostic = loadScenarioYaml(bsiDiagnosticPath.toUtf8().toStdString());
    auto profile = loadStandProfile(profilePath.toUtf8().toStdString());
    ScenarioEngine engine;
    registerUbsiProcedures(engine);
    registerTelemetryProcedures(engine);
    const auto errors = engine.validate(scenario);
    require(errors.empty(), errors.empty() ? "" : errors.front());
    require(scenario.publicationState == PublicationState::Published, "UBSI scenario must be published");
    require(engine.validate(bsiDiagnostic).empty(), "BSI diagnostic scenario must validate");
    require(bsiDiagnostic.publicationState == PublicationState::Published,
            "BSI diagnostic scenario must be published as a non-acceptance procedure");
    require(profile.id == "ktma-main" && !profile.activeOutputsConfirmed,
            "Default stand profile must keep active outputs locked");
    require(profile.routes.at("yalk_analog.type") == "1"
                && profile.routes.at("yalk_analog.max") == "4095"
                && profile.routes.at("yvp_input.safe") == "off",
            "Structured ISD route fields were not loaded from the profile");
    bool kpaProtocol = false;
    for (const auto& device : profile.devices) {
        if (device.pluginId == "orbita.ubsi_udp") {
            kpaProtocol = device.configuration.at("protocol") == "kpa_rokot_udp"
                && device.configuration.at("data_port") == "1113";
        }
    }
    require(kpaProtocol, "Default adapter profile must preserve the working KPA protocol candidate");

    std::set<std::string> scenarioCapabilities;
    std::function<void(const ScenarioNode&)> collectCapabilities = [&](const ScenarioNode& node) {
        scenarioCapabilities.insert(node.requiredCapabilities.begin(), node.requiredCapabilities.end());
        for (const auto& child : node.children) collectCapabilities(child);
    };
    for (const auto& step : scenario.steps) collectCapabilities(step);
    require(scenarioCapabilities.count("operator.manual_input") != 0
                && scenarioCapabilities.count("measure.reference_ac_voltage") != 0
                && scenarioCapabilities.count("measure.reference_frequency") != 0,
            "UBSI scenario must contain manual R4831 audit and V7 AC/frequency checks");
    require(scenarioCapabilities.count("signal.resistance") == 0
                && scenarioCapabilities.count("measure.waveform") == 0,
            "UBSI scenario must not require automatic R4831 or an oscilloscope");

    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create temporary directory");
    const QString db = QDir(temporary.path()).filePath(QStringLiteral("parameters.db"));
    require(QFile::copy(QDir(root).filePath(QStringLiteral("orbita/config/address/parameters.db")), db),
            "Cannot copy legacy database fixture");
    const auto catalog = importCatalogYaml(catalogPath.toUtf8().toStdString(), db.toUtf8().toStdString());
    require(catalog.blockTypes.size() == 2 && catalog.cellTypes.size() == 8
                && catalog.instances.size() == 2,
            "Normalized catalog has an unexpected object composition");
    const auto loaded = loadCatalog(db.toUtf8().toStdString());
    require(loaded.version == scenario.catalogVersion, "Scenario and catalog versions must match");

    const auto yalk = resolveCatalogParameterBinding(
        db.toUtf8().toStdString(), "UBSI_468157_002", "yalk_analog", 7);
    require(yalk.source == "ubsi.parameter_source" && yalk.locatorType == "ulk_address"
                && yalk.locator == "8" && yalk.mode == 6 && yalk.mask == 0x01FF,
            "YALK must resolve to the reference ULK address/mode from catalog");
    require(yalk.stimulusRoute == "yalk_analog" && yalk.stimulusOffset == 7,
            "YALK ISD stimulus route must resolve from catalog");
    const auto yalkAfterGap = resolveCatalogParameterBinding(
        db.toUtf8().toStdString(), "UBSI_468157_002", "yalk_analog", 28);
    require(yalkAfterGap.locator == "32" && yalkAfterGap.stimulusOffset == 31,
            "ULK address gaps must be preserved in data and ISD offset mappings");
    const auto yalkCalibration = resolveCatalogParameterBinding(
        db.toUtf8().toStdString(), "UBSI_468157_002", "yalk_calibration_full", 0);
    require(yalkCalibration.locatorType == "ulk_address" && yalkCalibration.locator == "99",
            "YALK calibration must use reference ULK address 99");
    const auto ytp = resolveCatalogParameterBinding(
        db.toUtf8().toStdString(), "UBSI_468157_002", "ytp_temperature", 3);
    require(ytp.source == "ubsi.parameter_source" && ytp.locatorType == "ulk_address"
                && ytp.locator == "4"
                && ytp.mode == 2,
            "YTP must resolve to ULK address and adapter mode 2 from catalog");
    const auto ytpCalibration = resolveCatalogParameterBinding(
        db.toUtf8().toStdString(), "UBSI_468157_002", "ytp_calibration_zero", 0);
    require(ytpCalibration.locator == "32",
            "YTP lower calibration must use reference ULK address 32");
    const auto yvp = resolveCatalogParameterBinding(
        db.toUtf8().toStdString(), "UBSI_468157_002", "yvp_fast", 0);
    require(yvp.source == "orbita.parameter_source"
                && yvp.locator == "M16P1A11B21T21",
            "YVP must resolve to an Orbita address from catalog");
    require(!yalk.confirmed && !ytp.confirmed && !yvp.confirmed,
            "Unverified live mappings must not allow acceptance OK");

    FakeEquipment diagnosticEquipment;
    diagnosticEquipment.capabilities.insert("orbita.parameter_source");
    const auto diagnosticRun = engine.run(
        bsiDiagnostic, diagnosticEquipment, profile.version, "", true);
    require(diagnosticRun.verdict == RunVerdict::Incomplete,
            "BSI stand diagnostic must never produce acceptance OK");
}

void builtinCapabilityBinding()
{
    EquipmentRegistry registry;
    bool stopped = false;
    registry.bind("orbita.parameter_source",
        [](const std::string& operation,
           const std::map<std::string, std::string>&) {
            return "operation=" + operation + "\n";
        },
        [&stopped]() { stopped = true; });
    require(registry.hasCapability("orbita.parameter_source"),
            "Built-in parameter source is not visible in the registry");
    require(registry.invoke("orbita.parameter_source", "probe", {})
                == "operation=probe\n",
            "Built-in capability did not receive invocation");
    registry.safeStopAll();
    require(stopped, "Built-in capability did not receive safe stop");
}

void waveformDecoder()
{
    const std::string preamble = "1,0,2,1,0.001,0,0,0.01,0,32768";
    std::vector<std::uint8_t> block{'#', '1', '4', 0x00, 0x80, 0x64, 0x80};
    const auto waveform = decodeDhoWordWaveform(preamble, block);
    require(waveform.volts.size() == 2, "DHO decoder lost waveform points");
    require(std::abs(waveform.volts[0]) < 1e-12 && std::abs(waveform.volts[1] - 1.0) < 1e-12,
            "DHO WORD conversion differs from verified unsigned format");
}

void pluginContracts()
{
    const QString pluginDirectory = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("../plugins"));
    EquipmentPluginManager manager;
    manager.loadDirectory(pluginDirectory.toUtf8().toStdString());
    require(manager.plugins().size() == 6, "All first-release equipment DLLs must load");
    bool ubsi = false;
    bool scope = false;
    bool akip = false;
    bool v7 = false;
    for (const auto& descriptor : manager.plugins()) {
        ubsi = ubsi || descriptor.id == "orbita.ubsi_udp";
        scope = scope || (descriptor.id == "orbita.rigol_dho8xx"
            && descriptor.capabilities.count("measure.waveform") != 0);
        akip = akip || (descriptor.id == "orbita.akip_1160_pair"
            && descriptor.capabilities.count("power.dc_supply") != 0);
        v7 = v7 || (descriptor.id == "orbita.v7_visa"
            && descriptor.capabilities.count("measure.reference_ac_voltage") != 0
            && descriptor.capabilities.count("measure.reference_frequency") != 0);
    }
    require(ubsi && scope && akip && v7,
            "Plugin ABI descriptors do not expose required capabilities");
}

void persistenceAndReport()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create report temporary directory");
    ScenarioRunResult run;
    run.runId = "run-1"; run.scenarioId = "s"; run.scenarioVersion = "1";
    run.catalogVersion = "c"; run.profileVersion = "p";
    run.startedAt = run.finishedAt = std::chrono::system_clock::now(); run.verdict = RunVerdict::Fail;
    MeasurementResult value{"p", "Параметр", 1, 2, 0.9, 1.1, "В", RunVerdict::Fail, "вне допуска"};
    StepRunResult step{"x", "Измерение", "1.1", RunVerdict::Fail, "вне допуска", {value}, {}};
    run.steps.push_back(step);
    RunStore store(QDir(temporary.path()).filePath(QStringLiteral("runs.db")).toUtf8().toStdString());
    store.save(run);
    const auto report = writeHtmlCsvReport(run, temporary.path().toUtf8().toStdString());
    require(QFile::exists(QString::fromUtf8(report.html)) && QFile::exists(QString::fromUtf8(report.csv)),
            "HTML/CSV report was not created");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try {
        engineSemantics();
        configurationAndCatalog(QStringLiteral(ORBITA_SOURCE_DIR));
        builtinCapabilityBinding();
        waveformDecoder();
        pluginContracts();
        persistenceAndReport();
        std::cout << "Scenario runtime tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Scenario runtime test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
