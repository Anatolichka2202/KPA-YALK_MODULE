#include "orbita_stand/run_store.h"

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

int main()
{
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
            database.close();
        }
        QSqlDatabase::removeDatabase(connection);

        std::cout << "RunStore project context persistence OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RunStore project context persistence failed: " << error.what() << '\n';
        return 1;
    }
}
