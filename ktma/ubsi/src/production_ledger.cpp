#include "ktma/ubsi/production_ledger.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QUuid>

#include <stdexcept>
#include <utility>

namespace ktma::ubsi {
namespace {

void check(bool ok, const QSqlQuery& query, const char* operation)
{
    if (!ok) throw std::runtime_error(
        std::string(operation) + ": " + query.lastError().text().toStdString());
}

std::string nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
}

std::string newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

} // namespace

struct ProductionLedger::Impl
{
    explicit Impl(std::string path)
        : connectionName(QStringLiteral("ktma_ubsi_production_%1")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
    {
        database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(QString::fromStdString(std::move(path)));
        if (!database.open()) {
            const auto message = database.lastError().text().toStdString();
            database = {};
            QSqlDatabase::removeDatabase(connectionName);
            throw std::runtime_error("cannot open UBSI production ledger: " + message);
        }
        QSqlQuery pragma(database);
        check(pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON")), pragma, "enable foreign keys");

        QSqlQuery query(database);
        check(query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS ubsi_production_runs ("
            "id TEXT PRIMARY KEY, product_id TEXT NOT NULL, product_serial TEXT NOT NULL, "
            "stage TEXT NOT NULL, package_code TEXT NOT NULL, scenario_code TEXT NOT NULL, "
            "status TEXT NOT NULL, run_id TEXT UNIQUE, opened_at TEXT NOT NULL, finished_at TEXT)")),
            query, "create ubsi_production_runs");
        check(query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_ubsi_production_product "
            "ON ubsi_production_runs(product_id, opened_at)")), query, "create product index");
        check(query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS ubsi_production_run_components ("
            "production_run_id TEXT NOT NULL, component_id TEXT NOT NULL, "
            "component_type TEXT NOT NULL, serial_number TEXT NOT NULL, affected INTEGER NOT NULL, "
            "PRIMARY KEY(production_run_id, component_id), "
            "FOREIGN KEY(production_run_id) REFERENCES ubsi_production_runs(id) ON DELETE CASCADE)")),
            query, "create ubsi_production_run_components");
    }

    ~Impl()
    {
        const QString name = connectionName;
        database.close();
        database = {};
        QSqlDatabase::removeDatabase(name);
    }

    ProductionRunRecord readRecord(QSqlQuery& query) const
    {
        ProductionRunRecord record;
        record.id = query.value(0).toString().toStdString();
        record.context.productId = query.value(1).toString().toStdString();
        record.context.productSerial = query.value(2).toString().toStdString();
        record.context.stage = registrar::stageFromString(query.value(3).toString().toStdString());
        record.context.package = productionPackageFromCode(query.value(4).toString().toStdString());
        record.context.scenarioCode = query.value(5).toString().toStdString();
        record.status = productionRunStatusFromString(query.value(6).toString().toStdString());
        record.runId = query.value(7).toString().toStdString();
        record.openedAt = query.value(8).toString().toStdString();
        record.finishedAt = query.value(9).toString().toStdString();

        QSqlQuery components(database);
        components.prepare(QStringLiteral(
            "SELECT component_id, component_type, serial_number, affected "
            "FROM ubsi_production_run_components WHERE production_run_id=? ORDER BY component_type"));
        components.addBindValue(QString::fromStdString(record.id));
        check(components.exec(), components, "read production snapshot");
        while (components.next()) {
            record.context.composition.push_back({
                components.value(0).toString().toStdString(),
                components.value(1).toString().toStdString(),
                components.value(2).toString().toStdString(),
                components.value(3).toInt() != 0});
        }
        return record;
    }

    QString connectionName;
    QSqlDatabase database;
};

ProductionLedger::ProductionLedger(std::string databasePath)
    : impl_(std::make_unique<Impl>(std::move(databasePath)))
{
}

ProductionLedger::~ProductionLedger() = default;

std::string ProductionLedger::begin(const ProductionRunContext& context)
{
    if (context.productId.empty() || context.productSerial.empty()
        || context.scenarioCode.empty() || context.composition.size() != 4) {
        throw std::invalid_argument("invalid UBSI ProductionRunContext");
    }

    const std::string id = newId();
    if (!impl_->database.transaction())
        throw std::runtime_error("cannot start UBSI production transaction");
    try {
        QSqlQuery run(impl_->database);
        run.prepare(QStringLiteral(
            "INSERT INTO ubsi_production_runs "
            "(id, product_id, product_serial, stage, package_code, scenario_code, status, opened_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
        run.addBindValue(QString::fromStdString(id));
        run.addBindValue(QString::fromStdString(context.productId));
        run.addBindValue(QString::fromStdString(context.productSerial));
        run.addBindValue(QString::fromUtf8(registrar::toString(context.stage)));
        run.addBindValue(QString::fromUtf8(toString(context.package)));
        run.addBindValue(QString::fromStdString(context.scenarioCode));
        run.addBindValue(QString::fromUtf8(toString(ProductionRunStatus::InProgress)));
        run.addBindValue(QString::fromStdString(nowUtc()));
        check(run.exec(), run, "insert production run");

        for (const auto& component : context.composition) {
            if (component.componentId.empty() || component.componentType.empty()
                || component.serialNumber.empty()) {
                throw std::invalid_argument("production composition snapshot is incomplete");
            }
            QSqlQuery item(impl_->database);
            item.prepare(QStringLiteral(
                "INSERT INTO ubsi_production_run_components "
                "(production_run_id, component_id, component_type, serial_number, affected) "
                "VALUES (?, ?, ?, ?, ?)"));
            item.addBindValue(QString::fromStdString(id));
            item.addBindValue(QString::fromStdString(component.componentId));
            item.addBindValue(QString::fromStdString(component.componentType));
            item.addBindValue(QString::fromStdString(component.serialNumber));
            item.addBindValue(component.affected ? 1 : 0);
            check(item.exec(), item, "insert production component snapshot");
        }
        if (!impl_->database.commit())
            throw std::runtime_error("cannot commit UBSI production run");
    } catch (...) {
        impl_->database.rollback();
        throw;
    }
    return id;
}

void ProductionLedger::attachRun(const std::string& productionRunId, const std::string& runId)
{
    if (productionRunId.empty() || runId.empty())
        throw std::invalid_argument("production run id and ScenarioEngine run_id are required");
    QSqlQuery query(impl_->database);
    query.prepare(QStringLiteral(
        "UPDATE ubsi_production_runs SET run_id=? WHERE id=? AND status='IN_PROGRESS'"));
    query.addBindValue(QString::fromStdString(runId));
    query.addBindValue(QString::fromStdString(productionRunId));
    check(query.exec(), query, "attach ScenarioEngine run");
    if (query.numRowsAffected() != 1)
        throw std::logic_error("production run is missing or already finished");
}

void ProductionLedger::finish(const std::string& productionRunId, ProductionRunStatus status)
{
    if (status == ProductionRunStatus::InProgress)
        throw std::invalid_argument("cannot finish production run as IN_PROGRESS");
    QSqlQuery query(impl_->database);
    query.prepare(QStringLiteral(
        "UPDATE ubsi_production_runs SET status=?, finished_at=? "
        "WHERE id=? AND status='IN_PROGRESS'"));
    query.addBindValue(QString::fromUtf8(toString(status)));
    query.addBindValue(QString::fromStdString(nowUtc()));
    query.addBindValue(QString::fromStdString(productionRunId));
    check(query.exec(), query, "finish production run");
    if (query.numRowsAffected() != 1)
        throw std::logic_error("production run is missing or already finished");
}

ProductionRunRecord ProductionLedger::get(const std::string& productionRunId) const
{
    QSqlQuery query(impl_->database);
    query.prepare(QStringLiteral(
        "SELECT id, product_id, product_serial, stage, package_code, scenario_code, "
        "status, COALESCE(run_id,''), opened_at, COALESCE(finished_at,'') "
        "FROM ubsi_production_runs WHERE id=?"));
    query.addBindValue(QString::fromStdString(productionRunId));
    check(query.exec(), query, "read production run");
    if (!query.next()) throw std::out_of_range("production run not found: " + productionRunId);
    return impl_->readRecord(query);
}

std::vector<ProductionRunRecord> ProductionLedger::listForProduct(const std::string& productId) const
{
    QSqlQuery query(impl_->database);
    query.prepare(QStringLiteral(
        "SELECT id, product_id, product_serial, stage, package_code, scenario_code, "
        "status, COALESCE(run_id,''), opened_at, COALESCE(finished_at,'') "
        "FROM ubsi_production_runs WHERE product_id=? ORDER BY opened_at DESC"));
    query.addBindValue(QString::fromStdString(productId));
    check(query.exec(), query, "list production runs");
    std::vector<ProductionRunRecord> result;
    while (query.next()) result.push_back(impl_->readRecord(query));
    return result;
}

} // namespace ktma::ubsi
