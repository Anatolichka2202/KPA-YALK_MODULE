#include "orbita_stand/run_store.h"
#include "orbita_stand/report_writer.h"
#include "orbita_stand/project.h"

#include <QCoreApplication>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace orbita::stand;

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void requireEvidenceOrder(const std::vector<EvidenceEvent>& evidence)
{
    require(!evidence.empty(), "evidence must not be empty");
    for (std::size_t index = 1; index < evidence.size(); ++index) {
        require(evidence[index].sequence == evidence[index - 1].sequence + 1,
            "evidence sequence must be contiguous");
        require(evidence[index].monotonicNs >= evidence[index - 1].monotonicNs,
            "evidence monotonic clock must not move backwards");
    }
}

class AuditEquipment final : public ICapabilityProvider {
public:
    bool hasCapability(const std::string& capability) const override
    {
        return capability == "test.echo";
    }

    std::string invoke(
        const std::string& capability,
        const std::string& operation,
        const std::map<std::string, std::string>& arguments) override
    {
        if (capability != "test.echo" || operation != "ping") {
            throw std::runtime_error("unexpected equipment invocation");
        }
        const auto value = arguments.find("value");
        if (value == arguments.end()) throw std::runtime_error("missing value");
        if (value->second == "error") throw std::runtime_error("simulated equipment error");
        return "echo=" + value->second;
    }

    void safeStopAll() noexcept override {}
};

ScenarioRunResult completedGenericRun()
{
    ScenarioDefinition scenario;
    scenario.id = "generic.persistence.contract";
    scenario.title = "Generic persistence contract";
    scenario.version = "1";
    scenario.catalogVersion = "test";
    scenario.objectType = "TEST";
    scenario.publicationState = PublicationState::Published;

    ScenarioNode step;
    step.id = "measure";
    step.title = "Measure";
    step.tuRequirement = "test";
    step.procedure = "test.measure";
    step.requiredCapabilities.insert("test.echo");
    scenario.steps.push_back(std::move(step));

    ScenarioEngine engine;
    engine.registerProcedure("test.measure", [](const ScenarioNode&, ProcedureContext& context) {
        const auto response = context.equipment.invoke("test.echo", "ping", {{"value", "42"}});
        MeasurementResult measurement;
        measurement.parameterKey = "test.voltage";
        measurement.title = "Test voltage";
        measurement.reference = 6.2;
        measurement.measured = response == "echo=42" ? 6.2 : 0.0;
        measurement.lowerLimit = 6.0;
        measurement.upperLimit = 6.4;
        measurement.unit = "V";
        measurement.verdict = measurement.measured == 6.2 ? RunVerdict::Ok : RunVerdict::Fail;
        measurement.message = "measured";
        measurement.attributes = {{"source", "generic-test"}};
        measurement.evidence = {MeasurementIdentityState::Provided,
            "measure.reference", "fake.voltmeter", "VALID"};
        return ProcedureResult{measurement.verdict, "completed", {measurement}};
    });

    AuditEquipment equipment;
    auto run = runScenarioWithEvidence(
        engine, equipment, scenario, "profile", "SN", false);
    run.runId = "context-run";
    run.projectId = "ktma";
    run.projectVersion = "1.0.0";
    run.workflowId = "tu_normal";
    run.dutType = "UBSI_468157_002";
    run.dutId = "dut";
    run.operatorName = "operator";
    run.environmentProfile = "normal.yaml";
    run.contextAttributes = {{"mode", "formal"}};
    return run;
}

ScenarioRunResult failedGenericRun()
{
    ScenarioDefinition scenario;
    scenario.id = "generic.evidence.error.contract";
    scenario.title = "Generic evidence error contract";
    scenario.version = "1";
    scenario.catalogVersion = "test";
    scenario.objectType = "TEST";
    scenario.publicationState = PublicationState::Published;

    ScenarioNode step;
    step.id = "error";
    step.title = "Error";
    step.tuRequirement = "test";
    step.procedure = "test.error";
    step.requiredCapabilities.insert("test.echo");
    scenario.steps.push_back(std::move(step));

    ScenarioEngine engine;
    engine.registerProcedure("test.error", [](const ScenarioNode&, ProcedureContext& context) {
        (void)context.equipment.invoke("test.echo", "ping", {{"value", "error"}});
        return ProcedureResult{RunVerdict::Ok, "unreachable", {}};
    });

    AuditEquipment equipment;
    auto run = runScenarioWithEvidence(
        engine, equipment, scenario, "profile", "SN", false);
    run.runId = "error-run";
    return run;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    try {
        QTemporaryDir directory;
        require(directory.isValid(), "temporary directory unavailable");
        const QString path = directory.filePath(QStringLiteral("runs.db"));

        auto run = completedGenericRun();
        require(run.verdict == RunVerdict::Ok, "generic run must complete before persistence");
        require(run.evidence.size() >= 4, "generic run must produce command and safety evidence");
        requireEvidenceOrder(run.evidence);
        const auto failedRun = failedGenericRun();
        require(failedRun.verdict == RunVerdict::Error, "error scenario must retain technical error");
        requireEvidenceOrder(failedRun.evidence);
        const auto errorEvidence = std::find_if(
            failedRun.evidence.begin(), failedRun.evidence.end(),
            [](const EvidenceEvent& event) { return event.type == "ERROR"; });
        require(errorEvidence != failedRun.evidence.end()
                    && errorEvidence->data.at("error") == "simulated equipment error",
            "equipment error must be structured evidence");
        RunArtifacts artifacts(directory.filePath(QStringLiteral("artifacts")).toUtf8().toStdString(), run.runId);
        artifacts.appendRawPacket({0x01, 0x02, 0x03});
        artifacts.attachTo(run);
        QFile waveform(QDir(QString::fromUtf8(artifacts.directory())).filePath(QStringLiteral("waveform.csv")));
        require(waveform.open(QIODevice::WriteOnly | QIODevice::Text), "cannot create test waveform");
        waveform.write("time_s;volts\n0;0\n");
        waveform.close();
        artifacts.attachFileTo(run, "waveform", "waveform.csv", "text/csv");
        require(run.artifacts.size() == 3 && !run.artifacts.front().sha256.empty(),
            "raw artifact metadata must be attached without generic events");

        {
            RunStore store(path.toUtf8().toStdString());
            store.save(run);
            store.save(failedRun);
        }

        const QString connection = QStringLiteral("run_store_context_test");
        {
            auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            database.setDatabaseName(path);
            require(database.open(), "cannot open run database");
            QSqlQuery query(database);
            require(query.exec(QStringLiteral(
                "SELECT project_id,project_version,workflow_id,dut_type,dut_id,operator_name,"
                "environment_profile,scenario_title,context_attributes FROM test_runs WHERE run_id='context-run'")),
                "cannot query project context");
            require(query.next(), "run row missing");
            require(query.value(0).toString() == QStringLiteral("ktma"), "project id missing");
            require(query.value(1).toString() == QStringLiteral("1.0.0"), "project version missing");
            require(query.value(2).toString() == QStringLiteral("tu_normal"), "workflow id missing");
            require(query.value(3).toString() == QStringLiteral("UBSI_468157_002"), "dut type missing");
            require(query.value(4).toString() == QStringLiteral("dut"), "dut id missing");
            require(query.value(5).toString() == QStringLiteral("operator"), "operator missing");
            require(query.value(6).toString() == QStringLiteral("normal.yaml"), "environment missing");
            require(query.value(7).toString() == QStringLiteral("Generic persistence contract"),
                "scenario title missing");
            require(query.value(8).toString().contains(QStringLiteral("mode=formal")),
                "context attributes missing");
            query.finish();

            require(query.exec(QStringLiteral(
                "SELECT sequence,monotonic_ns,type,node_id,resource,capability,operation,message,verdict,data "
                "FROM run_evidence WHERE run_id='context-run'")),
                "cannot query structured evidence");
            require(query.next(), "structured evidence row missing");
            require(query.value(0).toLongLong() == 1, "evidence sequence missing");
            require(query.value(1).toLongLong() > 0, "evidence monotonic time missing");
            require(query.value(2).toString() == QStringLiteral("COMMAND"), "evidence type missing");
            require(query.value(3).toString().isEmpty(), "evidence node must retain empty value");
            require(query.value(4).toString().isEmpty(), "evidence resource must retain empty value");
            require(query.value(5).toString() == QStringLiteral("test.echo"), "evidence capability missing");
            require(query.value(6).toString() == QStringLiteral("ping"), "evidence operation missing");
            require(query.value(7).toString() == QStringLiteral("Equipment command"), "evidence message missing");
            require(query.value(8).toString() == QStringLiteral("NOT_RUN"), "evidence verdict missing");
            require(query.value(9).toString().contains(QStringLiteral("arg.value=42")),
                "evidence data missing");
            query.finish();
            database.close();
        }
        QSqlDatabase::removeDatabase(connection);

        RunStore store(path.toUtf8().toStdString());
        const auto restored = store.load("context-run");
        require(restored.has_value(), "saved run must be loadable");
        require(!store.load("does-not-exist").has_value(), "missing run must return no result");
        require(restored->scenarioTitle == "Generic persistence contract", "title did not round-trip");
        require(restored->verdict == RunVerdict::Ok, "run verdict did not round-trip");
        require(restored->contextAttributes.at("mode") == "formal", "context did not round-trip");
        require(restored->steps.size() == 1 && restored->steps.front().measurements.size() == 1,
            "step measurement did not round-trip");
        const auto& measurement = restored->steps.front().measurements.front();
        require(measurement.parameterKey == "test.voltage" && measurement.measured == 6.2
                    && measurement.attributes.at("source") == "generic-test"
                    && measurement.evidence.identityState == MeasurementIdentityState::Provided
                    && measurement.evidence.resource == "measure.reference"
                    && measurement.evidence.device == "fake.voltmeter"
                    && measurement.evidence.quality == "VALID",
            "measurement contents did not round-trip");
        require(restored->evidence.size() == run.evidence.size(), "evidence count did not round-trip");
        require(restored->evidence.front().type == "COMMAND"
                    && restored->evidence.front().data.at("arg.value") == "42",
            "evidence contents did not round-trip");
        const auto restoredError = store.load("error-run");
        require(restoredError.has_value()
                    && std::any_of(restoredError->evidence.begin(), restoredError->evidence.end(),
                        [](const EvidenceEvent& event) { return event.type == "ERROR"; }),
            "error evidence did not round-trip");
        requireEvidenceOrder(restoredError->evidence);
        require(restored->artifactDirectory == run.artifactDirectory
                    && restored->artifacts.size() == 3
                    && restored->artifacts[1].kind == "raw-packets"
                    && restored->artifacts[1].relativePath == "raw_packets.bin"
                    && restored->artifacts[2].kind == "waveform"
                    && !restored->artifacts[1].sha256.empty(),
            "raw artifact metadata did not round-trip");

        const auto reportsDirectory = directory.filePath(QStringLiteral("reports"));
        const auto reports = writeHtmlCsvReport(*restored, reportsDirectory.toUtf8().toStdString());
        QFile report(QString::fromUtf8(reports.productionHtml));
        require(report.open(QIODevice::ReadOnly | QIODevice::Text), "cannot read rendered report");
        const auto rendered = QString::fromUtf8(report.readAll());
        require(rendered.contains(QStringLiteral("Generic persistence contract"))
                    && rendered.contains(QStringLiteral("НОРМА"))
                    && rendered.contains(QStringLiteral("raw_packets.bin"))
                    && rendered.contains(QStringLiteral("waveform.csv")),
            "report must be rendered from restored run");

        const QString legacyConnection = QStringLiteral("run_store_legacy_measurement_test");
        {
            auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), legacyConnection);
            database.setDatabaseName(path);
            require(database.open(), "cannot open run database for legacy migration");
            QSqlQuery query(database);
            require(query.exec(QStringLiteral(
                "UPDATE run_measurements SET evidence_state='' "
                "WHERE run_id='context-run' AND node_id='measure'")),
                "cannot simulate legacy empty measurement identity");
            database.close();
        }
        QSqlDatabase::removeDatabase(legacyConnection);
        RunStore migratedStore(path.toUtf8().toStdString());
        const auto migrated = migratedStore.load("context-run");
        require(migrated.has_value()
                    && migrated->steps.front().measurements.front().evidence.identityState
                        == MeasurementIdentityState::NotProvided,
            "legacy empty measurement identity must migrate to NOT_PROVIDED");

        std::cout << "RunStore generic run persistence and re-render OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RunStore project context and evidence persistence failed: " << error.what() << '\n';
        return 1;
    }
}
