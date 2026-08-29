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
#include <set>
#include <stdexcept>

namespace orbita::stand {
namespace {

std::atomic_uint runConnectionCounter{0};

qint64 milliseconds(std::chrono::system_clock::time_point value)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count();
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
    QString result;
    for (const auto& [key, value] : values) {
        result += QString::fromUtf8(key) + '=' + QString::fromUtf8(value).replace('\n', ' ') + '\n';
    }
    return result;
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
             "CREATE TABLE IF NOT EXISTS run_events(run_id TEXT NOT NULL REFERENCES test_runs(run_id) ON DELETE CASCADE,sort_order INTEGER NOT NULL,timestamp_ms INTEGER NOT NULL,node_id TEXT NOT NULL,stage TEXT NOT NULL,message TEXT NOT NULL,verdict TEXT NOT NULL,PRIMARY KEY(run_id,sort_order))"}) {
        execute(query, QString::fromLatin1(sql));
    }
    std::set<QString> measurementColumns;
    execute(query, QStringLiteral("PRAGMA table_info(run_measurements)"));
    while (query.next()) measurementColumns.insert(query.value(1).toString());
    if (!measurementColumns.count(QStringLiteral("attributes"))) execute(query,
        QStringLiteral("ALTER TABLE run_measurements ADD COLUMN attributes TEXT NOT NULL DEFAULT ''"));
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
            "INSERT INTO test_runs(run_id,scenario_id,scenario_version,catalog_version,profile_version,object_serial,started_ms,finished_ms,verdict) VALUES(?,?,?,?,?,?,?,?,?)"));
        query.addBindValue(QString::fromUtf8(run.runId)); query.addBindValue(QString::fromUtf8(run.scenarioId));
        query.addBindValue(QString::fromUtf8(run.scenarioVersion)); query.addBindValue(QString::fromUtf8(run.catalogVersion));
        query.addBindValue(QString::fromUtf8(run.profileVersion)); query.addBindValue(QString::fromUtf8(run.objectSerial));
        query.addBindValue(milliseconds(run.startedAt)); query.addBindValue(milliseconds(run.finishedAt));
        query.addBindValue(QString::fromLatin1(toString(run.verdict))); executePrepared(query);
        for (std::size_t index = 0; index < run.steps.size(); ++index) {
            saveStep(impl_->database, run.runId, {}, run.steps[index], static_cast<unsigned>(index));
        }
        query.prepare(QStringLiteral(
            "INSERT INTO run_events(run_id,sort_order,timestamp_ms,node_id,stage,message,verdict) VALUES(?,?,?,?,?,?,?)"));
        for (std::size_t index = 0; index < run.events.size(); ++index) {
            const auto& event = run.events[index];
            query.bindValue(0, QString::fromUtf8(run.runId)); query.bindValue(1, static_cast<unsigned>(index));
            query.bindValue(2, milliseconds(event.timestamp)); query.bindValue(3, QString::fromUtf8(event.nodeId));
            query.bindValue(4, QString::fromUtf8(event.stage)); query.bindValue(5, QString::fromUtf8(event.message));
            query.bindValue(6, QString::fromLatin1(toString(event.verdict))); executePrepared(query); query.finish();
        }
        if (!impl_->database.commit()) throw std::runtime_error(impl_->database.lastError().text().toUtf8().toStdString());
    } catch (...) {
        impl_->database.rollback();
        throw;
    }
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
