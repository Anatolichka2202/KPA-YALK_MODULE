#include "orbita_stand/run_store.h"
#include "orbita_stand/report_writer.h"
#include "orbita_stand/project.h"

#include <QCoreApplication>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace orbita::stand;

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
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
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    try {
        QTemporaryDir directory;
        require(directory.isValid(), "temporary directory unavailable");
        const QString path = directory.filePath(QStringLiteral("runs.db"));

        const auto run = completedGenericRun();
        require(run.verdict == RunVerdict::Ok, "generic run must complete before persistence");
        require(run.evidence.size() >= 4, "generic run must produce command and safety evidence");

        {
            RunStore store(path.toUtf8().toStdString());
            store.save(run);
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
                    && measurement.attributes.at("source") == "generic-test",
            "measurement contents did not round-trip");
        require(restored->evidence.size() == run.evidence.size(), "evidence count did not round-trip");
        require(restored->evidence.front().type == "COMMAND"
                    && restored->evidence.front().data.at("arg.value") == "42",
            "evidence contents did not round-trip");

        const auto reportsDirectory = directory.filePath(QStringLiteral("reports"));
        const auto reports = writeHtmlCsvReport(*restored, reportsDirectory.toUtf8().toStdString());
        QFile report(QString::fromUtf8(reports.productionHtml));
        require(report.open(QIODevice::ReadOnly | QIODevice::Text), "cannot read rendered report");
        const auto rendered = QString::fromUtf8(report.readAll());
        require(rendered.contains(QStringLiteral("Generic persistence contract"))
                    && rendered.contains(QStringLiteral("НОРМА")),
            "report must be rendered from restored run");

        std::cout << "RunStore generic run persistence and re-render OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RunStore project context and evidence persistence failed: " << error.what() << '\n';
        return 1;
    }
}
