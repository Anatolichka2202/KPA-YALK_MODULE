#include "orbita_stand/run_store.h"
#include "orbita_stand/report_writer.h"
#include "orbita_stand/project.h"
#include "orbita_stand/station_session.h"

#include <QCoreApplication>
#include <QFile>
#include <QElapsedTimer>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cstdlib>
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

    void safeStopAll() noexcept override { ++safeStops; }
    int safeStops = 0;
};

void runInterruptedChild(const std::string& databasePath, const QString& markerPath)
{
    RunStore store(databasePath);
    ScenarioDefinition scenario;
    scenario.id = "interrupted-simulator";
    scenario.title = "Прерванная проверка";
    scenario.version = "1";
    scenario.catalogVersion = "1";
    scenario.publicationState = PublicationState::Published;
    ScenarioNode step;
    step.id = "first";
    step.title = "Первое измерение";
    step.tuRequirement = "test.1";
    step.procedure = "test.measure";
    ScenarioNode group;
    group.id = "group";
    group.title = "Группа измерений";
    group.children.push_back(step);
    step.id = "second";
    group.children.push_back(step);
    scenario.steps.push_back(group);
    ScenarioEngine engine;
    engine.registerProcedure("test.measure", [](const ScenarioNode&, ProcedureContext& context) {
        (void)context.equipment.invoke("test.echo", "ping", {{"value", "42"}});
        MeasurementResult measurement;
        measurement.parameterKey = "test.value";
        measurement.title = "Имитируемое значение";
        measurement.measured = 42;
        measurement.verdict = RunVerdict::Ok;
        return ProcedureResult{RunVerdict::Ok, "Измерено", {measurement}};
    });
    AuditEquipment equipment;
    (void)runScenarioWithEvidence(engine, equipment, scenario, "simulator", "SN", false,
        {}, nullptr, [&](const ScenarioRunResult& snapshot) {
            if (snapshot.steps.empty()) store.save(snapshot);
            else store.saveCheckpoint(snapshot);
            if (!snapshot.steps.empty()) {
                QFile marker(markerPath);
                if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) std::_Exit(24);
                marker.write(snapshot.runId.data(), static_cast<qint64>(snapshot.runId.size()));
                marker.close();
                // Внезапное завершение не запускает деструкторы и не выполняет
                // последующие шаги или штатное завершение результата.
                std::_Exit(23);
            }
        });
    std::_Exit(25);
}

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
    const auto arguments = QCoreApplication::arguments();
    if (arguments.size() == 4 && arguments.at(1) == QStringLiteral("--interrupt-after-step")) {
        runInterruptedChild(arguments.at(2).toUtf8().toStdString(), arguments.at(3));
    }

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
        // Каждая фиксация заменяет предыдущий снимок одной транзакцией:
        // после сбоя остаётся последний подтверждённый шаг, а не пустой запуск.
        auto checkpoint = run;
        checkpoint.runId = "checkpoint-run";
        checkpoint.finishedAt = {};
        checkpoint.verdict = RunVerdict::Incomplete;
        checkpoint.steps.clear();
        checkpoint.events.clear();
        checkpoint.evidence.clear();
        checkpoint.artifacts.clear();
        checkpoint.artifactDirectory.clear();
        store.save(checkpoint);
        bool collisionRejected = false;
        try { store.save(checkpoint); } catch (const std::exception&) { collisionRejected = true; }
        require(collisionRejected, "a new run replaced an existing incomplete run");
        auto partial = store.load(checkpoint.runId);
        require(partial && partial->verdict == RunVerdict::Incomplete && partial->steps.empty(),
                "initial checkpoint was not persisted");
        checkpoint.steps.push_back(run.steps.front());
        store.saveCheckpoint(checkpoint);
        partial = store.load(checkpoint.runId);
        require(partial && partial->steps.size() == 1
                    && partial->steps.front().measurements.size() == 1,
                "finished step and measurement were not persisted");
        auto invalidCheckpoint = checkpoint;
        invalidCheckpoint.steps.push_back(checkpoint.steps.front());
        bool checkpointFailure = false;
        try { store.saveCheckpoint(invalidCheckpoint); }
        catch (const std::exception&) { checkpointFailure = true; }
        partial = store.load(checkpoint.runId);
        require(checkpointFailure && partial && partial->steps.size() == 1,
                "failed checkpoint did not preserve the previous transaction");
        checkpoint.finishedAt = run.finishedAt;
        checkpoint.verdict = RunVerdict::Ok;
        store.saveCheckpoint(checkpoint);
        bool completedRunProtected = false;
        try { store.saveCheckpoint(invalidCheckpoint); }
        catch (const std::exception&) { completedRunProtected = true; }
        partial = store.load(checkpoint.runId);
        require(completedRunProtected && partial && partial->verdict == RunVerdict::Ok,
                "completed run was replaced by a stale checkpoint");

        auto lockedRun = checkpoint;
        lockedRun.runId = "locked-run";
        lockedRun.finishedAt = {};
        lockedRun.verdict = RunVerdict::Incomplete;
        store.save(lockedRun);
        const QString lockConnection = QStringLiteral("checkpoint_lock_test");
        {
            auto blocker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), lockConnection);
            blocker.setDatabaseName(path);
            require(blocker.open(), "cannot open competing database connection");
            QSqlQuery lockQuery(blocker);
            require(lockQuery.exec(QStringLiteral("BEGIN EXCLUSIVE")), "cannot lock test database");
            QElapsedTimer elapsed;
            elapsed.start();
            bool lockedRejected = false;
            try { store.saveCheckpoint(lockedRun); }
            catch (const std::exception&) { lockedRejected = true; }
            require(lockedRejected && elapsed.elapsed() < 2000,
                    "database lock blocked checkpoint failure for too long");
            require(lockQuery.exec(QStringLiteral("ROLLBACK")), "cannot release test database lock");
            blocker.close();
        }
        QSqlDatabase::removeDatabase(lockConnection);
        require(store.load(lockedRun.runId).has_value(),
                "locked checkpoint damaged the previously stored run");

        ScenarioDefinition liveScenario;
        liveScenario.id = "live-check";
        liveScenario.title = "Проверка с имитатором";
        liveScenario.version = "1";
        liveScenario.catalogVersion = "1";
        liveScenario.publicationState = PublicationState::Published;
        ScenarioNode liveStep;
        liveStep.id = "first";
        liveStep.title = "Первое измерение";
        liveStep.tuRequirement = "test.1";
        liveStep.procedure = "test.live";
        liveScenario.steps.push_back(liveStep);
        liveStep.id = "second";
        liveScenario.steps.push_back(liveStep);
        ScenarioEngine liveEngine;
        int performed = 0;
        liveEngine.registerProcedure("test.live", [&](const ScenarioNode&, ProcedureContext& context) {
            const auto persisted = store.load(context.runId);
            require(persisted && persisted->verdict == RunVerdict::Incomplete
                        && persisted->contextAttributes.count("scenario_definition_json") == 1,
                    "run and its scenario snapshot were not persisted before simulated activity");
            require(persisted->steps.size() == static_cast<std::size_t>(performed),
                    "completed step was not persisted before the next one");
            ++performed;
            MeasurementResult measurement;
            measurement.parameterKey = "test.value";
            measurement.title = "Имитируемое значение";
            measurement.measured = performed;
            measurement.verdict = RunVerdict::Ok;
            return ProcedureResult{RunVerdict::Ok, "Измерено", {measurement}};
        });
        AuditEquipment simulatedEquipment;
        auto liveResult = runScenarioWithEvidence(liveEngine, simulatedEquipment, liveScenario,
            "simulator", "SN", false, {}, nullptr,
            [&](const ScenarioRunResult& snapshot) {
                if (snapshot.steps.empty()) store.save(snapshot);
                else store.saveCheckpoint(snapshot);
            });
        require(performed == 2 && liveResult.verdict == RunVerdict::Ok,
                "live scenario did not finish");
        const auto persistedLive = store.load(liveResult.runId);
        require(persistedLive && persistedLive->verdict == RunVerdict::Ok
                    && persistedLive->steps.size() == 2
                    && persistedLive->steps.front().measurements.size() == 1,
                "completed live result was not persisted");

        StationSession station;
        int simulatedCommands = 0;
        station.equipment().bind("test.echo", [&](const std::string&,
                const std::map<std::string, std::string>&) {
            ++simulatedCommands;
            return std::string("42");
        });
        station.scenarioEngine().registerProcedure("test.live", [&](const ScenarioNode&,
                ProcedureContext& context) {
            const auto persisted = store.load(context.runId);
            require(persisted && persisted->steps.size() == static_cast<std::size_t>(simulatedCommands),
                    "station did not expose the confirmed checkpoint before the next command");
            (void)context.equipment.invoke("test.echo", "measure", {});
            MeasurementResult measurement;
            measurement.parameterKey = "test.value";
            measurement.title = "Значение имитатора";
            measurement.measured = 42;
            measurement.verdict = RunVerdict::Ok;
            return ProcedureResult{RunVerdict::Ok, "Измерено", {measurement}};
        });
        auto applicationCheckpoints = makeIntermediateCheckpointSink(path.toUtf8().toStdString());
        const auto applicationResult = station.runScenario(liveScenario, "simulator", "SN", false,
                                                            {}, {}, applicationCheckpoints);
        const auto beforeFinalize = store.load(applicationResult.runId);
        require(simulatedCommands == 2 && beforeFinalize
                    && beforeFinalize->verdict == RunVerdict::Incomplete
                    && beforeFinalize->steps.size() == 2,
                "station did not persist both intermediate simulated measurements");
        store.saveCheckpoint(applicationResult);
        const auto finalized = store.load(applicationResult.runId);
        require(finalized && finalized->verdict == RunVerdict::Ok,
                "station result was not finalized after the worker completed");

        int writes = 0;
        performed = 0;
        const int stopsBeforeFailedStart = simulatedEquipment.safeStops;
        auto failedStart = runScenarioWithEvidence(liveEngine, simulatedEquipment, liveScenario,
            "simulator", "SN", false, {}, nullptr,
            [&](const ScenarioRunResult&) { throw std::runtime_error("database unavailable"); });
        require(performed == 0 && failedStart.persistenceFailed
                    && failedStart.verdict == RunVerdict::Error
                    && simulatedEquipment.safeStops == stopsBeforeFailedStart + 1,
                "failure to create the initial checkpoint allowed a simulated action");

        auto failedWrite = runScenarioWithEvidence(liveEngine, simulatedEquipment, liveScenario,
            "simulator", "SN", false, {}, nullptr,
            [&](const ScenarioRunResult& snapshot) {
                if (++writes == 2) throw std::runtime_error("simulated write failure");
                if (snapshot.steps.empty()) store.save(snapshot);
                else store.saveCheckpoint(snapshot);
            });
        require(performed == 1 && failedWrite.persistenceFailed
                    && failedWrite.verdict == RunVerdict::Error
                    && failedWrite.steps.size() == 1
                    && failedWrite.steps.front().measurements.size() == 1,
                "write failure did not stop subsequent actions or retain the unsaved measurement");
        const auto beforeFailure = store.load(failedWrite.runId);
        require(beforeFailure && beforeFailure->verdict == RunVerdict::Incomplete
                    && beforeFailure->steps.empty(),
                "failed write was reported as a confirmed step");

        const auto childDatabase = directory.filePath(QStringLiteral("interrupted.db"));
        const auto markerPath = directory.filePath(QStringLiteral("interrupted-run-id"));
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("--interrupt-after-step"), childDatabase, markerPath});
        require(child.waitForStarted(5000) && child.waitForFinished(10000)
                    && child.exitCode() == 23,
                "simulated process interruption did not occur after the first step");
        QFile marker(markerPath);
        require(marker.open(QIODevice::ReadOnly), "interrupted run id was not recorded");
        const auto interruptedId = marker.readAll().toStdString();
        marker.close();
        RunStore recoveredStore(childDatabase.toUtf8().toStdString());
        const auto recovered = recoveredStore.load(interruptedId);
        require(recovered && recovered->verdict == RunVerdict::Incomplete
                    && recovered->finishedAt == std::chrono::system_clock::time_point{}
                    && recovered->steps.size() == 1
                    && recovered->steps.front().nodeId == "group"
                    && recovered->steps.front().children.size() == 1
                    && recovered->steps.front().children.front().nodeId == "first"
                    && recovered->steps.front().children.front().measurements.size() == 1,
                "interrupted run did not retain the nested confirmed measurement");
        const auto unfinished = recoveredStore.unfinishedRuns();
        require(unfinished.size() == 1 && unfinished.front().runId == interruptedId
                    && unfinished.front().steps.front().children.front().measurements.front().measured == 42,
                "interrupted run was not available in the recovery list");

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
