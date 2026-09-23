#include "orbita_stand/run_store.h"

#include <QCoreApplication>
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
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    try {
        QTemporaryDir directory;
        require(directory.isValid(), "temporary directory unavailable");
        const QString path = directory.filePath(QStringLiteral("runs.db"));

        ScenarioRunResult run;
        run.runId = "context-run";
        run.scenarioId = "scenario";
        run.scenarioVersion = "1";
        run.catalogVersion = "catalog";
        run.profileVersion = "profile";
        run.objectSerial = "SN";
        run.startedAt = std::chrono::system_clock::now();
        run.finishedAt = run.startedAt;
        run.verdict = RunVerdict::Ok;
        run.projectId = "ktma";
        run.projectVersion = "1.0.0";
        run.workflowId = "tu_normal";
        run.dutType = "UBSI_468157_002";
        run.dutId = "dut";
        run.operatorName = "operator";
        run.environmentProfile = "normal.yaml";
        run.contextAttributes = {{"mode", "formal"}};

        EvidenceEvent evidence;
        evidence.sequence = 1;
        evidence.timestamp = run.startedAt;
        evidence.monotonicNs = 123456789;
        evidence.type = "COMMAND";
        evidence.nodeId = "power";
        evidence.resource = "power.dut";
        evidence.capability = "power.dc_supply";
        evidence.operation = "set_voltage";
        evidence.message = "Equipment command";
        evidence.data = {{"arg.volts", "27.0"}};
        run.evidence.push_back(std::move(evidence));

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
                "environment_profile,context_attributes FROM test_runs WHERE run_id='context-run'")),
                "cannot query project context");
            require(query.next(), "run row missing");
            require(query.value(0).toString() == QStringLiteral("ktma"), "project id missing");
            require(query.value(1).toString() == QStringLiteral("1.0.0"), "project version missing");
            require(query.value(2).toString() == QStringLiteral("tu_normal"), "workflow id missing");
            require(query.value(3).toString() == QStringLiteral("UBSI_468157_002"), "dut type missing");
            require(query.value(4).toString() == QStringLiteral("dut"), "dut id missing");
            require(query.value(5).toString() == QStringLiteral("operator"), "operator missing");
            require(query.value(6).toString() == QStringLiteral("normal.yaml"), "environment missing");
            require(query.value(7).toString().contains(QStringLiteral("mode=formal")),
                "context attributes missing");
            query.finish();

            require(query.exec(QStringLiteral(
                "SELECT sequence,monotonic_ns,type,node_id,resource,capability,operation,message,verdict,data "
                "FROM run_evidence WHERE run_id='context-run'")),
                "cannot query structured evidence");
            require(query.next(), "structured evidence row missing");
            require(query.value(0).toLongLong() == 1, "evidence sequence missing");
            require(query.value(1).toLongLong() == 123456789, "evidence monotonic time missing");
            require(query.value(2).toString() == QStringLiteral("COMMAND"), "evidence type missing");
            require(query.value(3).toString() == QStringLiteral("power"), "evidence node missing");
            require(query.value(4).toString() == QStringLiteral("power.dut"), "evidence resource missing");
            require(query.value(5).toString() == QStringLiteral("power.dc_supply"), "evidence capability missing");
            require(query.value(6).toString() == QStringLiteral("set_voltage"), "evidence operation missing");
            require(query.value(7).toString() == QStringLiteral("Equipment command"), "evidence message missing");
            require(query.value(8).toString() == QStringLiteral("NOT_RUN"), "evidence verdict missing");
            require(query.value(9).toString().contains(QStringLiteral("arg.volts=27.0")),
                "evidence data missing");
            require(!query.next(), "unexpected duplicate evidence row");
            query.finish();
            database.close();
        }
        QSqlDatabase::removeDatabase(connection);

        std::cout << "RunStore project context and evidence persistence OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RunStore project context and evidence persistence failed: " << error.what() << '\n';
        return 1;
    }
}
