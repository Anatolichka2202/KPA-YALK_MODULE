#include "orbita_stand/run_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>

#include <atomic>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace orbita::stand {
namespace {

std::atomic_uint runConnectionCounter{0};

qint64 milliseconds(std::chrono::system_clock::time_point value)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count();
}

std::chrono::system_clock::time_point fromMilliseconds(qint64 value)
{
    return std::chrono::system_clock::time_point{std::chrono::milliseconds(value)};
}

void execute(QSqlQuery& query, const QString& sql)
{
    if (!query.exec(sql)) throw std::runtime_error(query.lastError().text().toUtf8().toStdString());
}

void executePrepared(QSqlQuery& query)
{
    if (!query.exec()) throw std::runtime_error(query.lastError().text().toUtf8().toStdString());
}

QString attributesText(const std::map<std::string, std::string>& values)
{
    // A default-constructed QString is SQL NULL. Columns are NOT NULL,
    // therefore an empty attribute set must be bound as an empty string.
    QString result = QStringLiteral("");
    for (const auto& [key, value] : values) {
        result += QString::fromUtf8(key) + '=' + QString::fromUtf8(value).replace('\n', ' ') + '\n';
    }
    return result;
}

std::map<std::string, std::string> attributesFromText(const QString& text)
{
    std::map<std::string, std::string> values;
    for (const auto& line : text.split('\n', Qt::SkipEmptyParts)) {
        const auto separator = line.indexOf('=');
        if (separator <= 0) {
            throw std::runtime_error(
                "Persisted attribute line has no key/value separator");
        }
        const auto key = line.left(separator).toUtf8().toStdString();
        const auto value = line.mid(separator + 1).toUtf8().toStdString();
        const auto inserted = values.emplace(key, value);
        if (!inserted.second) {
            throw std::runtime_error("Persisted attribute key occurs more than once: " + key);
        }
    }
    return values;
}

RunVerdict verdictFromText(const QString& value)
{
    if (value == QStringLiteral("NOT_RUN")) return RunVerdict::NotRun;
    if (value == QStringLiteral("OK")) return RunVerdict::Ok;
    if (value == QStringLiteral("FAIL")) return RunVerdict::Fail;
    if (value == QStringLiteral("INCOMPLETE")) return RunVerdict::Incomplete;
    if (value == QStringLiteral("ERROR")) return RunVerdict::Error;
    if (value == QStringLiteral("ABORTED")) return RunVerdict::Aborted;
    throw std::runtime_error("Unknown persisted run verdict: " + value.toUtf8().toStdString());
}

void addTextColumnIfMissing(
    QSqlDatabase& database,
    const QString& table,
    const QString& column)
{
    QSqlQuery query(database);
    std::set<QString> columns;
    execute(query, QStringLiteral("PRAGMA table_info(%1)").arg(table));
    while (query.next()) columns.insert(query.value(1).toString());
    if (columns.count(column)) return;

    execute(query, QStringLiteral(
        "ALTER TABLE %1 ADD COLUMN %2 TEXT NOT NULL DEFAULT ''")
        .arg(table, column));
}

void saveStep(QSqlDatabase& database, const std::string& runId, const std::string& parent,
              const StepRunResult& step, unsigned order)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO run_steps(run_id,node_id,parent_node,sort_order,title,tu_requirement,verdict,message) VALUES(?,?,?,?,?,?,?,?)"));
    query.addBindValue(QString::fromUtf8(runId)); query.addBindValue(QString::fromUtf8(step.nodeId));
    query.addBindValue(QString::fromUtf8(parent)); query.addBindValue(order);
    query.addBindValue(QString::fromUtf8(step.title)); query.addBindValue(QString::fromUtf8(step.tuRequirement));
    query.addBindValue(QString::fromLatin1(toString(step.verdict))); query.addBindValue(QString::fromUtf8(step.message));
    executePrepared(query);
    query.prepare(QStringLiteral(
        "INSERT INTO run_measurements(run_id,node_id,sort_order,parameter_key,title,reference,measured,lower_limit,upper_limit,unit,verdict,message,attributes) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    for (std::size_t index = 0; index < step.measurements.size(); ++index) {
        const auto& value = step.measurements[index];
        query.bindValue(0, QString::fromUtf8(runId)); query.bindValue(1, QString::fromUtf8(step.nodeId));
        query.bindValue(2, static_cast<unsigned>(index)); query.bindValue(3, QString::fromUtf8(value.parameterKey));
        query.bindValue(4, QString::fromUtf8(value.title)); query.bindValue(5, value.reference);
        query.bindValue(6, value.measured); query.bindValue(7, value.lowerLimit); query.bindValue(8, value.upperLimit);
        query.bindValue(9, QString::fromUtf8(value.unit)); query.bindValue(10, QString::fromLatin1(toString(value.verdict)));
        query.bindValue(11, QString::fromUtf8(value.message));
        query.bindValue(12, attributesText(value.attributes)); executePrepared(query); query.finish();
    }
    for (std::size_t index = 0; index < step.children.size(); ++index) {
        saveStep(database, runId, step.nodeId, step.children[index], static_cast<unsigned>(index));
    }
}

} // namespace

struct RunStore::Impl {
    QString connectionName;
    QSqlDatabase database;
};

RunStore::RunStore(std::string sqlitePath) : impl_(std::make_unique<Impl>())
{
    impl_->connectionName = QStringLiteral("orbita_runs_%1").arg(++runConnectionCounter);
    impl_->database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), impl_->connectionName);
    impl_->database.setDatabaseName(QString::fromUtf8(sqlitePath));
    if (!impl_->database.open()) throw std::runtime_error(impl_->database.lastError().text().toUtf8().toStdString());
    QSqlQuery query(impl_->database);
    for (const auto& sql : {
             "PRAGMA foreign_keys = ON",
             "CREATE TABLE IF NOT EXISTS test_runs(run_id TEXT PRIMARY KEY,scenario_id TEXT NOT NULL,scenario_version TEXT NOT NULL,catalog_version TEXT NOT NULL,profile_version TEXT NOT NULL,object_serial TEXT NOT NULL,started_ms INTEGER NOT NULL,finished_ms INTEGER NOT NULL,verdict TEXT NOT NULL)",
             "CREATE TABLE IF NOT EXISTS run_steps(run_id TEXT NOT NULL REFERENCES test_runs(run_id) ON DELETE CASCADE,node_id TEXT NOT NULL,parent_node TEXT NOT NULL,sort_order INTEGER NOT NULL,title TEXT NOT NULL,tu_requirement TEXT NOT NULL,verdict TEXT NOT NULL,message TEXT NOT NULL,PRIMARY KEY(run_id,node_id))",
             "CREATE TABLE IF NOT EXISTS run_measurements(run_id TEXT NOT NULL,node_id TEXT NOT NULL,sort_order INTEGER NOT NULL,parameter_key TEXT NOT NULL,title TEXT NOT NULL,reference REAL NOT NULL,measured REAL NOT NULL,lower_limit REAL NOT NULL,upper_limit REAL NOT NULL,unit TEXT NOT NULL,verdict TEXT NOT NULL,message TEXT NOT NULL,attributes TEXT NOT NULL DEFAULT '',PRIMARY KEY(run_id,node_id,sort_order),FOREIGN KEY(run_id,node_id) REFERENCES run_steps(run_id,node_id) ON DELETE CASCADE)",
             "CREATE TABLE IF NOT EXISTS run_events(run_id TEXT NOT NULL REFERENCES test_runs(run_id) ON DELETE CASCADE,sort_order INTEGER NOT NULL,timestamp_ms INTEGER NOT NULL,node_id TEXT NOT NULL,stage TEXT NOT NULL,message TEXT NOT NULL,verdict TEXT NOT NULL,PRIMARY KEY(run_id,sort_order))",
             "CREATE TABLE IF NOT EXISTS run_evidence(run_id TEXT NOT NULL REFERENCES test_runs(run_id) ON DELETE CASCADE,sequence INTEGER NOT NULL,timestamp_ms INTEGER NOT NULL,monotonic_ns INTEGER NOT NULL,type TEXT NOT NULL,node_id TEXT NOT NULL,resource TEXT NOT NULL,capability TEXT NOT NULL,operation TEXT NOT NULL,message TEXT NOT NULL,verdict TEXT NOT NULL,data TEXT NOT NULL DEFAULT '',PRIMARY KEY(run_id,sequence))"}) {
        execute(query, QString::fromLatin1(sql));
    }

    // Additive migration: existing station databases remain readable. Empty
    // values mean that the run was created through the legacy raw-scenario API.
    for (const auto& column : {
             QStringLiteral("project_id"),
             QStringLiteral("project_version"),
             QStringLiteral("workflow_id"),
             QStringLiteral("dut_type"),
             QStringLiteral("dut_id"),
             QStringLiteral("operator_name"),
             QStringLiteral("environment_profile"),
             QStringLiteral("scenario_title"),
             QStringLiteral("context_attributes")}) {
        addTextColumnIfMissing(impl_->database, QStringLiteral("test_runs"), column);
    }

    addTextColumnIfMissing(
        impl_->database, QStringLiteral("run_events"), QStringLiteral("attributes"));
    addTextColumnIfMissing(
        impl_->database, QStringLiteral("run_measurements"), QStringLiteral("attributes"));
}

RunStore::~RunStore()
{
    if (!impl_) return;
    impl_->database.close();
    const auto name = impl_->connectionName;
    impl_->database = {};
    QSqlDatabase::removeDatabase(name);
}

void RunStore::save(const ScenarioRunResult& run)
{
    if (!impl_->database.transaction()) throw std::runtime_error("Cannot start run log transaction");
    try {
        QSqlQuery query(impl_->database);
        query.prepare(QStringLiteral(
            "INSERT INTO test_runs("
            "run_id,scenario_id,scenario_version,catalog_version,profile_version,object_serial,"
            "started_ms,finished_ms,verdict,project_id,project_version,workflow_id,dut_type,dut_id,"
            "operator_name,environment_profile,scenario_title,context_attributes) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        query.addBindValue(QString::fromUtf8(run.runId));
        query.addBindValue(QString::fromUtf8(run.scenarioId));
        query.addBindValue(QString::fromUtf8(run.scenarioVersion));
        query.addBindValue(QString::fromUtf8(run.catalogVersion));
        query.addBindValue(QString::fromUtf8(run.profileVersion));
        query.addBindValue(QString::fromUtf8(run.objectSerial));
        query.addBindValue(milliseconds(run.startedAt));
        query.addBindValue(milliseconds(run.finishedAt));
        query.addBindValue(QString::fromLatin1(toString(run.verdict)));
        query.addBindValue(QString::fromUtf8(run.projectId));
        query.addBindValue(QString::fromUtf8(run.projectVersion));
        query.addBindValue(QString::fromUtf8(run.workflowId));
        query.addBindValue(QString::fromUtf8(run.dutType));
        query.addBindValue(QString::fromUtf8(run.dutId));
        query.addBindValue(QString::fromUtf8(run.operatorName));
        query.addBindValue(QString::fromUtf8(run.environmentProfile));
        query.addBindValue(QString::fromUtf8(run.scenarioTitle));
        query.addBindValue(attributesText(run.contextAttributes));
        executePrepared(query);

        for (std::size_t index = 0; index < run.steps.size(); ++index) {
            saveStep(impl_->database, run.runId, {}, run.steps[index], static_cast<unsigned>(index));
        }

        query.prepare(QStringLiteral(
            "INSERT INTO run_events(run_id,sort_order,timestamp_ms,node_id,stage,message,verdict,attributes) VALUES(?,?,?,?,?,?,?,?)"));
        for (std::size_t index = 0; index < run.events.size(); ++index) {
            const auto& event = run.events[index];
            query.bindValue(0, QString::fromUtf8(run.runId)); query.bindValue(1, static_cast<unsigned>(index));
            query.bindValue(2, milliseconds(event.timestamp)); query.bindValue(3, QString::fromUtf8(event.nodeId));
            query.bindValue(4, QString::fromUtf8(event.stage)); query.bindValue(5, QString::fromUtf8(event.message));
            query.bindValue(6, QString::fromLatin1(toString(event.verdict)));
            query.bindValue(7, attributesText(event.data)); executePrepared(query); query.finish();
        }

        query.prepare(QStringLiteral(
            "INSERT INTO run_evidence(run_id,sequence,timestamp_ms,monotonic_ns,type,node_id,resource,capability,operation,message,verdict,data) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
        for (const auto& event : run.evidence) {
            query.bindValue(0, QString::fromUtf8(run.runId));
            query.bindValue(1, static_cast<qulonglong>(event.sequence));
            query.bindValue(2, milliseconds(event.timestamp));
            query.bindValue(3, static_cast<qlonglong>(event.monotonicNs));
            query.bindValue(4, QString::fromUtf8(event.type));
            query.bindValue(5, QString::fromUtf8(event.nodeId));
            query.bindValue(6, QString::fromUtf8(event.resource));
            query.bindValue(7, QString::fromUtf8(event.capability));
            query.bindValue(8, QString::fromUtf8(event.operation));
            query.bindValue(9, QString::fromUtf8(event.message));
            query.bindValue(10, QString::fromLatin1(toString(event.verdict)));
            query.bindValue(11, attributesText(event.data));
            executePrepared(query);
            query.finish();
        }

        if (!impl_->database.commit()) throw std::runtime_error(impl_->database.lastError().text().toUtf8().toStdString());
    } catch (...) {
        impl_->database.rollback();
        throw;
    }
}

std::optional<ScenarioRunResult> RunStore::load(const std::string& runId) const
{
    QSqlQuery query(impl_->database);
    query.prepare(QStringLiteral(
        "SELECT scenario_id,scenario_title,scenario_version,catalog_version,profile_version,object_serial,"
        "started_ms,finished_ms,verdict,project_id,project_version,workflow_id,dut_type,dut_id,"
        "operator_name,environment_profile,context_attributes "
        "FROM test_runs WHERE run_id=?"));
    query.addBindValue(QString::fromUtf8(runId));
    executePrepared(query);
    if (!query.next()) return std::nullopt;

    ScenarioRunResult result;
    result.runId = runId;
    result.scenarioId = query.value(0).toString().toUtf8().toStdString();
    result.scenarioTitle = query.value(1).toString().toUtf8().toStdString();
    result.scenarioVersion = query.value(2).toString().toUtf8().toStdString();
    result.catalogVersion = query.value(3).toString().toUtf8().toStdString();
    result.profileVersion = query.value(4).toString().toUtf8().toStdString();
    result.objectSerial = query.value(5).toString().toUtf8().toStdString();
    result.startedAt = fromMilliseconds(query.value(6).toLongLong());
    result.finishedAt = fromMilliseconds(query.value(7).toLongLong());
    result.verdict = verdictFromText(query.value(8).toString());
    result.projectId = query.value(9).toString().toUtf8().toStdString();
    result.projectVersion = query.value(10).toString().toUtf8().toStdString();
    result.workflowId = query.value(11).toString().toUtf8().toStdString();
    result.dutType = query.value(12).toString().toUtf8().toStdString();
    result.dutId = query.value(13).toString().toUtf8().toStdString();
    result.operatorName = query.value(14).toString().toUtf8().toStdString();
    result.environmentProfile = query.value(15).toString().toUtf8().toStdString();
    result.contextAttributes = attributesFromText(query.value(16).toString());
    if (query.next()) throw std::runtime_error("Persisted run id is not unique: " + runId);
    query.finish();

    std::map<std::string, std::vector<MeasurementResult>> measurements;
    query.prepare(QStringLiteral(
        "SELECT node_id,parameter_key,title,reference,measured,lower_limit,upper_limit,unit,verdict,message,attributes "
        "FROM run_measurements WHERE run_id=? ORDER BY node_id,sort_order"));
    query.addBindValue(QString::fromUtf8(runId));
    executePrepared(query);
    while (query.next()) {
        MeasurementResult value;
        const auto nodeId = query.value(0).toString().toUtf8().toStdString();
        value.parameterKey = query.value(1).toString().toUtf8().toStdString();
        value.title = query.value(2).toString().toUtf8().toStdString();
        value.reference = query.value(3).toDouble();
        value.measured = query.value(4).toDouble();
        value.lowerLimit = query.value(5).toDouble();
        value.upperLimit = query.value(6).toDouble();
        value.unit = query.value(7).toString().toUtf8().toStdString();
        value.verdict = verdictFromText(query.value(8).toString());
        value.message = query.value(9).toString().toUtf8().toStdString();
        value.attributes = attributesFromText(query.value(10).toString());
        measurements[nodeId].push_back(std::move(value));
    }
    query.finish();

    struct StoredStep {
        StepRunResult step;
        std::string parent;
    };
    std::map<std::string, std::vector<StoredStep>> children;
    query.prepare(QStringLiteral(
        "SELECT node_id,parent_node,title,tu_requirement,verdict,message "
        "FROM run_steps WHERE run_id=? ORDER BY parent_node,sort_order"));
    query.addBindValue(QString::fromUtf8(runId));
    executePrepared(query);
    while (query.next()) {
        StoredStep stored;
        stored.step.nodeId = query.value(0).toString().toUtf8().toStdString();
        stored.parent = query.value(1).toString().toUtf8().toStdString();
        stored.step.title = query.value(2).toString().toUtf8().toStdString();
        stored.step.tuRequirement = query.value(3).toString().toUtf8().toStdString();
        stored.step.verdict = verdictFromText(query.value(4).toString());
        stored.step.message = query.value(5).toString().toUtf8().toStdString();
        const auto found = measurements.find(stored.step.nodeId);
        if (found != measurements.end()) stored.step.measurements = found->second;
        children[stored.parent].push_back(std::move(stored));
    }
    query.finish();

    std::function<std::vector<StepRunResult>(const std::string&)> restoreSteps;
    restoreSteps = [&children, &restoreSteps](const std::string& parent) {
        std::vector<StepRunResult> restored;
        const auto found = children.find(parent);
        if (found == children.end()) return restored;
        restored.reserve(found->second.size());
        for (const auto& stored : found->second) {
            auto step = stored.step;
            step.children = restoreSteps(step.nodeId);
            restored.push_back(std::move(step));
        }
        return restored;
    };
    result.steps = restoreSteps({});

    query.prepare(QStringLiteral(
        "SELECT timestamp_ms,node_id,stage,message,verdict,attributes "
        "FROM run_events WHERE run_id=? ORDER BY sort_order"));
    query.addBindValue(QString::fromUtf8(runId));
    executePrepared(query);
    while (query.next()) {
        RunEvent event;
        event.timestamp = fromMilliseconds(query.value(0).toLongLong());
        event.nodeId = query.value(1).toString().toUtf8().toStdString();
        event.stage = query.value(2).toString().toUtf8().toStdString();
        event.message = query.value(3).toString().toUtf8().toStdString();
        event.verdict = verdictFromText(query.value(4).toString());
        event.data = attributesFromText(query.value(5).toString());
        result.events.push_back(std::move(event));
    }
    query.finish();

    query.prepare(QStringLiteral(
        "SELECT sequence,timestamp_ms,monotonic_ns,type,node_id,resource,capability,operation,message,verdict,data "
        "FROM run_evidence WHERE run_id=? ORDER BY sequence"));
    query.addBindValue(QString::fromUtf8(runId));
    executePrepared(query);
    while (query.next()) {
        EvidenceEvent event;
        event.sequence = query.value(0).toULongLong();
        event.timestamp = fromMilliseconds(query.value(1).toLongLong());
        event.monotonicNs = query.value(2).toLongLong();
        event.type = query.value(3).toString().toUtf8().toStdString();
        event.nodeId = query.value(4).toString().toUtf8().toStdString();
        event.resource = query.value(5).toString().toUtf8().toStdString();
        event.capability = query.value(6).toString().toUtf8().toStdString();
        event.operation = query.value(7).toString().toUtf8().toStdString();
        event.message = query.value(8).toString().toUtf8().toStdString();
        event.verdict = verdictFromText(query.value(9).toString());
        event.data = attributesFromText(query.value(10).toString());
        result.evidence.push_back(std::move(event));
    }
    return result;
}

RunArtifacts::RunArtifacts(std::string rootDirectory, std::string runId)
{
    QDir root(QString::fromUtf8(rootDirectory));
    if (!root.mkpath(QString::fromUtf8(runId))) throw std::runtime_error("Cannot create run artifact directory");
    directory_ = root.filePath(QString::fromUtf8(runId)).toUtf8().toStdString();
    QDir directory(QString::fromUtf8(directory_));
    telemetryPath_ = directory.filePath(QStringLiteral("telemetry.csv")).toUtf8().toStdString();
    rawPath_ = directory.filePath(QStringLiteral("raw_packets.bin")).toUtf8().toStdString();
    QFile telemetry(QString::fromUtf8(telemetryPath_));
    if (!telemetry.open(QIODevice::WriteOnly | QIODevice::Text)) throw std::runtime_error("Cannot create telemetry.csv");
    telemetry.write("timestamp_ms;sequence;parameter_key;raw;physical;unit;quality;diagnostic\n");
}

void RunArtifacts::appendTelemetry(const ParameterSample& sample)
{
    QFile file(QString::fromUtf8(telemetryPath_));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) throw std::runtime_error("Cannot append telemetry");
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << milliseconds(sample.timestamp) << ';' << sample.sequence << ';'
           << QString::fromUtf8(sample.parameterKey) << ';' << QString::number(sample.rawValue, 'g', 15) << ';'
           << QString::number(sample.physicalValue, 'g', 15) << ';' << QString::fromUtf8(sample.unit) << ';'
           << static_cast<int>(sample.quality) << ';' << QString::fromUtf8(sample.diagnostic).replace(';', ',') << '\n';
}

void RunArtifacts::appendRawPacket(const std::vector<std::uint8_t>& bytes)
{
    QFile file(QString::fromUtf8(rawPath_));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) throw std::runtime_error("Cannot append raw packet");
    const std::uint32_t size = static_cast<std::uint32_t>(bytes.size());
    file.write(reinterpret_cast<const char*>(&size), sizeof(size));
    if (!bytes.empty()) file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
}

const std::string& RunArtifacts::directory() const noexcept { return directory_; }

} // namespace orbita::stand
