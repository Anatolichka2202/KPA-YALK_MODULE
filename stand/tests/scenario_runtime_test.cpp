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
#include <algorithm>
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
                       const std::map<std::string, std::string>& arguments) override
    {
        operations.push_back(capability + ":" + operation);
        if (capability == "orbita.parameter_source" && operation == "health") {
            return "status=ready\nframes_processed=12\nphrase_error_percent=0\n"
                   "group_error_percent=0\nchannel_count=8\n";
        }
        if (capability == "ulk.parameter_source" && operation == "start_record") {
            return "status=ok\npath=fake/frames.ulkbin\n";
        }
        if (capability == "ulk.parameter_source" && operation == "start_prepared_yalk_reference") {
            return "status=ready\nfirst_sequence=1\n";
        }
        if (capability == "ulk.parameter_source" && operation == "stats") {
            return "status=ready\nlast_sequence=1\n";
        }
        if (capability == "stand.switch_matrix" && operation == "yalk_set_voltage") {
            currentVoltage = std::stod(arguments.at("volts"));
            return "status=ready\n";
        }
        if (capability == "stand.switch_matrix" && operation == "yalk_output_off") {
            currentVoltage = 0.0;
            return "status=ready\n";
        }
        if (capability == "measure.reference_voltage") {
            const double reference = currentVoltage == 6.2 ? 6.145 : currentVoltage;
            return "status=ready\nvolts=" + std::to_string(reference) + "\n";
        }
        if (capability == "ulk.parameter_source" && operation == "read_channel") {
            const unsigned address = static_cast<unsigned>(std::stoul(arguments.at("ulk_address")));
            const double reference = currentVoltage == 6.2 ? 6.145 : currentVoltage;
            const bool open = forceOpenReading && address < 97 && currentVoltage == 0.0;
            const double code = open ? 0.0 : address == 97 ? 125.0 : address == 99 ? 925.0
                : 125.0 + reference / 6.2 * 800.0;
            return "status=ready\nraw_mean=" + std::to_string(code)
                + "\nanalog_code_mean=" + std::to_string(code)
                + "\nsignal=" + std::string(open || currentVoltage >= 6.2 ? "1" : "0")
                + "\nsample_count=16\nfirst_sequence=2\nlast_sequence=17\n";
        }
        if (capability == "ulk.parameter_source" && operation == "read_snapshot") {
            std::string words;
            for (unsigned index = 0; index < 100; ++index) {
                if (index) words += ',';
                words += std::to_string(500u + index);
            }
            return "status=ready\nsequence=" + std::to_string(++snapshotSequence)
                + "\nwords=" + words + "\n";
        }
        if (capability == "ulk.parameter_source" && operation == "await_ytp_rokt") {
            return "status=ready\nprotocol=rokt_ytp68\nvalid_word_count=32\n"
                   "invalid_word_count=0\nfirst_sequence=1\n";
        }
        if (capability == "ulk.parameter_source" && operation == "read_ytp_channel") {
            const unsigned address = static_cast<unsigned>(std::stoul(arguments.at("ulk_address")));
            const double raw = address == 31 ? 4000.0 : address == 32 ? 330.0
                : 330.0 + currentResistance * (4000.0 - 330.0) / 240.0;
            return "status=ready\nprotocol=rokt_ytp68\nraw_mean=" + std::to_string(raw)
                + "\ntemperature_mode=0\nsample_count=16\nvalid_sample_count=16\n"
                  "invalid_sample_count=0\nfirst_sequence=2\nlast_sequence=17\n";
        }
        if (capability == "catalog.parameter_resolver" && operation == "resolve") {
            const std::string group = arguments.at("parameter_group");
            const unsigned index = static_cast<unsigned>(std::stoul(arguments.at("channel_index")));
            if (group.rfind("yalk_", 0) == 0) {
                const unsigned locator = group == "yalk_calibration_zero" ? 97
                    : group == "yalk_calibration_full" ? 99 : index + 1;
                return "source=ulk.parameter_source\nlocator_type=ulk_address\nlocator="
                    + std::to_string(locator)
                    + "\nstream_id=\nword_index=" + std::to_string(locator - 1)
                    + "\nmask=1023\nshift=0\nmode=0\nconversion_id=\n"
                      "stimulus_route=yalk_analog\nstimulus_offset=0\nconfirmed=true\n";
            }
            const unsigned locator = group == "ytp_temperature" ? index + 1
                : (group == "ytp_calibration_zero" ? 32 : 31);
            return "source=ulk.parameter_source\nlocator_type=ulk_address\nlocator="
                + std::to_string(locator)
                + "\nstream_id=\nword_index=" + std::to_string(locator - 1)
                + "\nmask=65535\nshift=0\nmode=2\nconversion_id=\n"
                  "stimulus_route=\nstimulus_offset=0\nconfirmed=true\n";
        }
        if (capability == "operator.manual_input" && operation == "confirm_value") {
            currentResistance = std::stod(arguments.at("target_value"));
            return "status=confirmed\nvalue=" + std::to_string(currentResistance)
                + "\noperator=test\ntimestamp=2026-09-02T12:00:00\n";
        }
        if (capability == "power.dc_supply" && operation == "read_state") {
            return "status=ready\nvolts=" + std::string(supplyOutputEnabled ? "27.0" : "0.0")
                + "\namperes=" + std::string(supplyOutputEnabled ? "0.2" : "0.0")
                + "\noutput_enabled=" + (supplyOutputEnabled ? "true" : "false") + "\n";
        }
        if (capability == "power.dc_supply" && operation == "output") {
            supplyOutputEnabled = arguments.at("enabled") == "true";
            if (supplyOutputEnabled) ++supplyEnableCount;
            else ++supplyDisableCount;
            return std::string("status=ok\noutput_enabled=")
                + (supplyOutputEnabled ? "true\n" : "false\n");
        }
        return "status=ready\n";
    }
    void safeStopAll() noexcept override { stopped = true; }
    std::set<std::string> capabilities;
    std::vector<std::string> operations;
    bool stopped = false;
    double currentResistance = 120.0;
    double currentVoltage = 0.0;
    bool supplyOutputEnabled = true;
    unsigned supplyEnableCount = 0;
    unsigned supplyDisableCount = 0;
    unsigned snapshotSequence = 20;
    bool forceOpenReading = false;
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
    require(combineVerdicts(RunVerdict::Incomplete, RunVerdict::Fail)
                == RunVerdict::Fail,
            "FAIL must not be masked by a commissioning INCOMPLETE verdict");
    require(combineVerdicts(RunVerdict::Fail, RunVerdict::Incomplete)
                == RunVerdict::Fail,
            "Later INCOMPLETE steps must preserve an earlier FAIL verdict");

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
    const auto ytpScenarioPath = QDir(root).filePath(QStringLiteral("data/scenarios/ubsi_ytp_tu_5_6.yaml"));
    const auto ytp120Path = QDir(root).filePath(QStringLiteral("data/scenarios/ubsi_ytp_120_check.yaml"));
    const auto combinedPath = QDir(root).filePath(QStringLiteral("data/scenarios/ubsi_ulk_combined_check.yaml"));
    const auto profilePath = QDir(root).filePath(QStringLiteral("data/profiles/stand_ktma.yaml"));
    const auto catalogPath = QDir(root).filePath(QStringLiteral("data/catalog/catalog.yaml"));
    auto scenario = loadScenarioYaml(scenarioPath.toUtf8().toStdString());
    auto bsiDiagnostic = loadScenarioYaml(bsiDiagnosticPath.toUtf8().toStdString());
    auto ytpScenario = loadScenarioYaml(ytpScenarioPath.toUtf8().toStdString());
    auto ytp120 = loadScenarioYaml(ytp120Path.toUtf8().toStdString());
    auto combined = loadScenarioYaml(combinedPath.toUtf8().toStdString());
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
    const auto ytpValidationErrors = engine.validate(ytpScenario);
    require(ytpValidationErrors.empty(), ytpValidationErrors.empty()
                ? "Standalone YTP scenario must validate"
                : ytpValidationErrors.front());
    require(ytpScenario.publicationState == PublicationState::Published
                && ytpScenario.steps.size() == 6,
            "YTP must be a published six-stage powered manual-reference scenario");
    require(engine.validate(ytp120).empty() && ytp120.steps.size() == 6,
            "Fixed 120-ohm YTP scenario must validate");
    require(engine.validate(combined).empty() && combined.steps.size() == 12,
            "Combined powered YALK/YTP delivery scenario must validate as twelve stages");
    require(profile.id == "ktma-main" && profile.activeOutputsConfirmed,
            "Verified stand profile must allow the captured active commands");
    require(profile.routes.at("yalk_analog.base") == "1"
                && profile.routes.at("yalk_analog.safe") == "off"
                && profile.routes.count("ytp_channel.base") == 0,
            "Structured ISD route fields were not loaded from the profile");
    require(profile.connections.at("adapter_rs485") == "Адаптер RS-485 → X1 ЯП-П"
                && profile.connections.at("isd_to_yalk") == "ИСД → X1, X2, X3 ЯЛК",
            "Confirmed stand cable topology was not loaded from the profile");
    bool liveAdapterProtocol = false;
    for (const auto& device : profile.devices) {
        if (device.pluginId == "orbita.ktma_adapter_udp") {
            liveAdapterProtocol = device.configuration.at("protocol") == "rokt_yalk"
                && device.configuration.at("host") == "192.168.0.115"
                && device.configuration.at("data_port") == "1113";
        }
    }
    require(liveAdapterProtocol, "Default adapter profile must use the verified ROKT YALK protocol");

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
        db.toUtf8().toStdString(), "UBSI_468157_002", "yalk_voltage", 7);
    require(yalk.source == "ulk.parameter_source" && yalk.locatorType == "ulk_address"
                && yalk.locator == "8" && yalk.mode == 0 && yalk.mask == 0x03FF,
            "YALK must resolve to the reference ULK address/mode from catalog");
    require(yalk.stimulusRoute == "yalk_analog" && yalk.stimulusOffset == 7,
            "YALK ISD stimulus route must resolve from catalog");
    const auto yalkAfterGap = resolveCatalogParameterBinding(
        db.toUtf8().toStdString(), "UBSI_468157_002", "yalk_voltage", 28);
    require(yalkAfterGap.locator == "32" && yalkAfterGap.stimulusOffset == 31,
            "ULK address gaps must be preserved in data and ISD offset mappings");
    const auto yalkCalibration = resolveCatalogParameterBinding(
        db.toUtf8().toStdString(), "UBSI_468157_002", "yalk_calibration_full", 0);
    require(yalkCalibration.locatorType == "ulk_address" && yalkCalibration.locator == "99",
            "YALK calibration must use reference ULK address 99");
    const auto ytp = resolveCatalogParameterBinding(
        db.toUtf8().toStdString(), "UBSI_468157_002", "ytp_temperature", 3);
    require(ytp.source == "ulk.parameter_source" && ytp.locatorType == "ulk_address"
                && ytp.locator == "4"
                && ytp.mode == 2,
            "YTP must resolve to ULK address and adapter mode 2 from catalog");
    for (unsigned channel = 0; channel < 30; ++channel) {
        const auto mapped = resolveCatalogParameterBinding(
            db.toUtf8().toStdString(), "UBSI_468157_002", "ytp_temperature", channel);
        require(mapped.locator == std::to_string(channel + 1)
                    && mapped.stimulusRoute.empty() && mapped.confirmed,
                "YTP catalog must map 30 frame words without an ISD channel route");
    }
    const auto ytpCalibration = resolveCatalogParameterBinding(
        db.toUtf8().toStdString(), "UBSI_468157_002", "ytp_calibration_zero", 0);
    require(ytpCalibration.locator == "32" && ytpCalibration.confirmed,
            "YTP lower calibration must use reference ULK address 32");
    const auto yvp = resolveCatalogParameterBinding(
        db.toUtf8().toStdString(), "UBSI_468157_002", "yvp_fast", 0);
    require(yvp.source == "orbita.parameter_source"
                && yvp.locator == "M16P1A11B21T21",
            "YVP must resolve to an Orbita address from catalog");
    require(yalk.confirmed && ytp.confirmed && !yvp.confirmed,
            "Live-confirmed YALK/YTP bindings must allow acceptance OK");

    FakeEquipment diagnosticEquipment;
    diagnosticEquipment.capabilities.insert("orbita.parameter_source");
    const auto diagnosticRun = engine.run(
        bsiDiagnostic, diagnosticEquipment, profile.version, "", true);
    require(diagnosticRun.verdict == RunVerdict::Incomplete,
            "BSI stand diagnostic must never produce acceptance OK");

    FakeEquipment ytpEquipment;
    ytpEquipment.capabilities = {
        "ulk.parameter_source", "stand.switch_matrix",
        "catalog.parameter_resolver", "operator.manual_input", "power.dc_supply"};
    const auto ytpRun = engine.run(
        ytpScenario, ytpEquipment, profile.version, "", false);
    require(ytpRun.verdict == RunVerdict::Ok,
            "Confirmed YTP manual-reference run must produce acceptance OK");
    require(std::find(ytpEquipment.operations.begin(), ytpEquipment.operations.end(),
                "ulk.parameter_source:stop_stream") != ytpEquipment.operations.end()
            && std::find(ytpEquipment.operations.begin(), ytpEquipment.operations.end(),
                "ulk.parameter_source:stop_record") != ytpEquipment.operations.end(),
            "YTP scenario must stop both stream and raw recording");
    require(std::count(ytpEquipment.operations.begin(), ytpEquipment.operations.end(),
                "operator.manual_input:confirm_value") == 3,
            "YTP must request all three manual R4831 points");
    require(std::count(ytpEquipment.operations.begin(), ytpEquipment.operations.end(),
                "stand.switch_matrix:switch") == 8,
            "YTP must enable and disable the four captured ISD type-7 routes");
    require(!ytpEquipment.supplyOutputEnabled,
            "YTP scenario must switch the AKIP output off after the test");
    const auto repeatedYtpRun = engine.run(
        ytpScenario, ytpEquipment, profile.version, "", false);
    require(repeatedYtpRun.verdict == RunVerdict::Ok
                && ytpEquipment.supplyEnableCount == 1
                && ytpEquipment.supplyDisableCount >= 2
                && !ytpEquipment.supplyOutputEnabled,
            "A repeated YTP run must restore AKIP output and switch it off again");
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

void productionYalkScaleRegression()
{
    ScenarioEngine engine;
    registerUbsiProcedures(engine);
    ScenarioDefinition scenario;
    scenario.id = "yalk-regression";
    scenario.title = "YALK scale regression";
    scenario.version = "1";
    scenario.catalogVersion = "1";
    scenario.objectType = "UBSI_468157_002";
    scenario.publicationState = PublicationState::Published;
    scenario.steps = {
        {"stream", "Поток", "5.6", "yalk.start_stream",
         {"ulk.parameter_source", "stand.switch_matrix"},
         {{"configure_settle_ms", "1"}, {"timeout_ms", "10"}}, {}},
        {"cal", "Калибровка", "5.6", "yalk.read_calibration",
         {"catalog.parameter_resolver", "ulk.parameter_source", "stand.switch_matrix",
          "measure.reference_voltage"},
         {{"channel_count", "1"}, {"sample_count", "1"}, {"settle_ms", "1"},
          {"full_voltage", "6.2"}}, {}},
        {"channels", "Канал", "5.6", "yalk.check_channels",
         {"catalog.parameter_resolver", "ulk.parameter_source", "stand.switch_matrix",
          "measure.reference_voltage"},
         {{"channel_count", "1"}, {"point_volts", "0,3.1,6.2"},
          {"sample_count", "1"}, {"settle_ms", "1"}, {"channel_off_settle_ms", "1"},
          {"full_scale_v", "6.2"}, {"tolerance_percent_fs", "0.5"}}, {}}
    };
    FakeEquipment equipment;
    equipment.capabilities = {"ulk.parameter_source", "stand.switch_matrix",
        "catalog.parameter_resolver", "measure.reference_voltage"};
    const auto run = engine.run(scenario, equipment, "p1", "", false);
    if (run.verdict != RunVerdict::Ok) {
        for (const auto& step : run.steps) {
            std::cerr << step.nodeId << ": " << toString(step.verdict) << " " << step.message << '\n';
            for (const auto& item : step.measurements)
                std::cerr << "  " << item.title << ": " << item.measured << " vs "
                          << item.reference << " => " << toString(item.verdict) << '\n';
        }
    }
    require(run.verdict == RunVerdict::Ok,
        "YALK 97/99 conversion must use the nominal 6.2 V scale, not the V7 calibration reading");
    const auto& channelStep = run.steps.back();
    require(!channelStep.measurements.empty()
                && channelStep.measurements.back().parameterKey
                    == "ubsi.yalk.contacts.coverage"
                && channelStep.measurements.back().measured == 1.0,
            "YALK run must report explicit contact-state coverage");
}

void yalkOverloadSequenceRegression()
{
    ScenarioEngine engine;
    registerUbsiProcedures(engine);
    ScenarioDefinition scenario;
    scenario.id = "yalk-overload-regression";
    scenario.title = "YALK overload sequence regression";
    scenario.version = "1";
    scenario.catalogVersion = "1";
    scenario.objectType = "UBSI_468157_002";
    scenario.publicationState = PublicationState::Published;
    scenario.steps = {
        {"stream", "Поток", "5.6", "yalk.start_stream",
         {"ulk.parameter_source", "stand.switch_matrix"},
         {{"configure_settle_ms", "1"}, {"timeout_ms", "10"}}, {}},
        {"cal", "Калибровка", "5.6", "yalk.read_calibration",
         {"catalog.parameter_resolver", "ulk.parameter_source", "stand.switch_matrix",
          "measure.reference_voltage"},
         {{"channel_count", "1"}, {"sample_count", "1"}, {"settle_ms", "1"},
          {"full_voltage", "6.2"}}, {}},
        {"overload", "Перегрузка", "1.1.4.10", "yalk.check_overload",
         {"ulk.parameter_source", "stand.switch_matrix"},
         {{"mapping_confirmed", "true"}, {"physical_channel_count", "2"},
          {"observed_address_count", "2"}, {"sample_count", "1"},
          {"baseline_settle_ms", "1"}, {"settle_ms", "1"},
          {"full_scale_v", "6.2"}, {"tolerance_percent_fs", "0.5"}}, {}}
    };
    FakeEquipment equipment;
    equipment.capabilities = {"ulk.parameter_source", "stand.switch_matrix",
        "catalog.parameter_resolver", "measure.reference_voltage"};
    const auto run = engine.run(scenario, equipment, "p1", "", false);
    require(run.verdict == RunVerdict::Ok && run.steps.size() == 3
                && run.steps.back().measurements.size() == 4,
            "YALK overload must test both polarities for every physical channel");
    require(std::count(equipment.operations.begin(), equipment.operations.end(),
                "ulk.parameter_source:read_snapshot") == 5,
            "YALK overload must save one baseline and read one fresh snapshot per impact");
}

void yalkOpenStateRegression()
{
    ScenarioEngine engine;
    registerUbsiProcedures(engine);
    ScenarioDefinition scenario;
    scenario.id = "yalk-open-regression";
    scenario.title = "YALK open state regression";
    scenario.version = "1";
    scenario.catalogVersion = "1";
    scenario.objectType = "UBSI_468157_002";
    scenario.publicationState = PublicationState::Published;
    scenario.steps = {
        {"stream", "Поток", "5.6", "yalk.start_stream",
         {"ulk.parameter_source", "stand.switch_matrix"},
         {{"configure_settle_ms", "1"}, {"timeout_ms", "10"}}, {}},
        {"cal", "Калибровка", "5.6", "yalk.read_calibration",
         {"catalog.parameter_resolver", "ulk.parameter_source", "stand.switch_matrix",
          "measure.reference_voltage"},
         {{"channel_count", "1"}, {"sample_count", "1"}, {"settle_ms", "1"},
          {"full_voltage", "6.2"}}, {}},
        {"initial", "Обрыв", "1.1.4.10", "yalk.check_initial_state",
         {"catalog.parameter_resolver", "ulk.parameter_source", "stand.switch_matrix"},
         {{"channel_count", "1"}, {"sample_count", "1"}, {"full_scale_v", "6.2"}}, {}}
    };
    FakeEquipment equipment;
    equipment.forceOpenReading = true;
    equipment.capabilities = {"ulk.parameter_source", "stand.switch_matrix",
        "catalog.parameter_resolver", "measure.reference_voltage"};
    const auto run = engine.run(scenario, equipment, "p1", "", false);
    require(run.verdict == RunVerdict::Ok && run.steps.back().measurements.size() == 2,
        "Negative YALK open value with signal=1 must satisfy the TU open-state criterion");
    require(run.steps.back().measurements.front().measured < 0.0,
        "YALK open-state regression must exercise a negative calibrated voltage");
}

void yalkCleanupSignedResidualRegression()
{
    ScenarioEngine engine;
    registerUbsiProcedures(engine);
    ScenarioDefinition scenario;
    scenario.id = "yalk-cleanup-signed-residual-regression";
    scenario.title = "YALK cleanup signed residual regression";
    scenario.version = "1";
    scenario.catalogVersion = "1";
    scenario.objectType = "UBSI_468157_002";
    scenario.publicationState = PublicationState::Published;
    scenario.steps = {
        {"cleanup", "Сброс", "5.6", "yalk.safe_cleanup",
         {"ulk.parameter_source", "stand.switch_matrix", "measure.reference_voltage"},
         {{"settle_ms", "1"}, {"retry_settle_ms", "1"},
          {"maximum_residual_voltage_v", "0.2"}}, {}}
    };
    FakeEquipment equipment;
    equipment.currentVoltage = -0.019;
    equipment.capabilities = {"ulk.parameter_source", "stand.switch_matrix",
        "measure.reference_voltage"};
    const auto run = engine.run(scenario, equipment, "p1", "", false);
    require(run.verdict == RunVerdict::Ok && run.steps.size() == 1
                && run.steps.front().measurements.size() == 1,
            "Signed cleanup residual inside +/- limit must satisfy the TU cleanup check");
    const auto& residual = run.steps.front().measurements.front();
    require(residual.lowerLimit == -0.2 && residual.upperLimit == 0.2,
            "YALK cleanup residual must be checked by absolute magnitude");
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
        ubsi = ubsi || descriptor.id == "orbita.ktma_adapter_udp";
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
    require(QFile::exists(QString::fromUtf8(report.html))
                && QFile::exists(QString::fromUtf8(report.productionHtml))
                && QFile::exists(QString::fromUtf8(report.csv)),
            "TU HTML, production HTML and CSV reports were not created");
    QFile html(QString::fromUtf8(report.html));
    require(html.open(QIODevice::ReadOnly), "Cannot open HTML report");
    const auto htmlText = html.readAll();
    require(htmlText.contains("НЕ НОРМА") && htmlText.contains("Измерений не в норме"),
            "TU report must use the operator-facing НОРМА / НЕ НОРМА wording and counts");

    ScenarioRunResult ytpRun = run;
    ytpRun.runId = "ytp-run-1";
    ytpRun.scenarioId = "ubsi.468157.002.ytp.tu5_6";
    ytpRun.scenarioTitle = "ЯТП commissioning";
    MeasurementResult ytpValue{
        "ubsi.ytp.1", "ЯТП канал 1", 100.0, 100.2, 98.8, 101.2,
        "Ом", RunVerdict::Ok, {}};
    ytpValue.attributes = {
        {"ytp_channel", "1"}, {"target_resistance_ohm", "100"},
        {"actual_reference_ohm", "100.0"}, {"raw", "1234"},
        {"calibration_zero_raw", "10"}, {"calibration_full_raw", "3000"},
        {"measured_resistance_ohm", "100.2"}, {"absolute_error_ohm", "0.2"},
        {"reduced_error_percent", "0.0833"}, {"operator", "tester"},
        {"timestamp", "2026-09-01T12:00:00"}};
    ytpRun.steps = {{"ytp", "Каналы ЯТП", "5.6", RunVerdict::Ok,
                     "ok", {ytpValue}, {}}};
    const auto ytpReport = writeHtmlCsvReport(
        ytpRun, temporary.path().toUtf8().toStdString());
    QFile ytpCsv(QString::fromUtf8(ytpReport.csv));
    require(ytpCsv.open(QIODevice::ReadOnly), "Cannot open YTP CSV report");
    const auto ytpCsvText = ytpCsv.readAll();
    require(ytpCsvText.contains("Канал ЯТП") && ytpCsvText.contains("Эталон, Ом")
                && ytpCsvText.contains("100.2"),
            "YTP CSV report must expose channel/reference/raw/ohm fields");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try {
        engineSemantics();
        configurationAndCatalog(QStringLiteral(ORBITA_SOURCE_DIR));
        builtinCapabilityBinding();
        productionYalkScaleRegression();
        yalkOpenStateRegression();
        yalkCleanupSignedResidualRegression();
        yalkOverloadSequenceRegression();
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
