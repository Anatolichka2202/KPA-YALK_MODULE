#include "registrar.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <array>
#include <algorithm>
#include <stdexcept>

namespace ktma::registrar {
namespace {

QString nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

std::string makeId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

[[noreturn]] void throwSql(const QSqlQuery& query, const char* operation)
{
    throw std::runtime_error(
        std::string(operation) + ": " + query.lastError().text().toStdString());
}

void execOrThrow(QSqlDatabase& database, const QString& sql, const char* operation)
{
    QSqlQuery query(database);
    if (!query.exec(sql)) throwSql(query, operation);
}

QString stageValue(Stage stage)
{
    return QString::fromUtf8(toString(stage));
}

QString verdictValue(Verdict verdict)
{
    return QString::fromUtf8(toString(verdict));
}

} // namespace

const char* toString(Stage stage)
{
    switch (stage) {
    case Stage::InitialElectrical: return "InitialElectrical";
    case Stage::PostVibrationElectrical: return "PostVibrationElectrical";
    case Stage::PostClimateElectrical: return "PostClimateElectrical";
    case Stage::FinalElectrical: return "FinalElectrical";
    }
    return "Unknown";
}

const char* toString(Verdict verdict)
{
    switch (verdict) {
    case Verdict::InProgress: return "InProgress";
    case Verdict::Ok: return "Ok";
    case Verdict::Fail: return "Fail";
    case Verdict::Cancelled: return "Cancelled";
    case Verdict::Incomplete: return "Incomplete";
    }
    return "Unknown";
}

Stage stageFromString(const std::string& value)
{
    if (value == "InitialElectrical") return Stage::InitialElectrical;
    if (value == "PostVibrationElectrical") return Stage::PostVibrationElectrical;
    if (value == "PostClimateElectrical") return Stage::PostClimateElectrical;
    if (value == "FinalElectrical") return Stage::FinalElectrical;
    throw std::invalid_argument("unknown registrar stage: " + value);
}

Verdict verdictFromString(const std::string& value)
{
    if (value == "InProgress") return Verdict::InProgress;
    if (value == "Ok") return Verdict::Ok;
    if (value == "Fail") return Verdict::Fail;
    if (value == "Cancelled") return Verdict::Cancelled;
    if (value == "Incomplete") return Verdict::Incomplete;
    throw std::invalid_argument("unknown registrar verdict: " + value);
}

Registrar::Registrar(const std::string& databasePath)
    : connectionName_(QStringLiteral("ktma_registrar_")
                      + QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    database_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    database_.setDatabaseName(QString::fromStdString(databasePath));
    if (!database_.open()) {
        throw std::runtime_error(database_.lastError().text().toStdString());
    }

    execOrThrow(database_, QStringLiteral("PRAGMA foreign_keys = ON"),
                "enable foreign keys");
    initializeSchema();
}

Registrar::~Registrar()
{
    if (database_.isValid()) database_.close();
    const QString connection = connectionName_;
    database_ = QSqlDatabase();
    if (!connection.isEmpty()) QSqlDatabase::removeDatabase(connection);
}

void Registrar::initializeSchema()
{
    execOrThrow(database_, QStringLiteral(
        "CREATE TABLE IF NOT EXISTS products ("
        "id TEXT PRIMARY KEY, product_type TEXT NOT NULL,"
        "serial_number TEXT NOT NULL UNIQUE,"
        "created_at TEXT NOT NULL DEFAULT ''"
        ")"), "create products");
    ensureColumn("products", "created_at", "TEXT NOT NULL DEFAULT ''");

    execOrThrow(database_, QStringLiteral(
        "CREATE TABLE IF NOT EXISTS components ("
        "id TEXT PRIMARY KEY, component_type TEXT NOT NULL,"
        "serial_number TEXT NOT NULL, created_at TEXT NOT NULL DEFAULT '',"
        "UNIQUE(component_type, serial_number)"
        ")"), "create components");
    ensureColumn("components", "created_at", "TEXT NOT NULL DEFAULT ''");

    execOrThrow(database_, QStringLiteral(
        "CREATE TABLE IF NOT EXISTS product_components ("
        "id TEXT PRIMARY KEY, product_id TEXT NOT NULL, component_id TEXT NOT NULL,"
        "installed_at TEXT NOT NULL, removed_at TEXT, removal_reason TEXT,"
        "active INTEGER NOT NULL DEFAULT 1,"
        "FOREIGN KEY(product_id) REFERENCES products(id),"
        "FOREIGN KEY(component_id) REFERENCES components(id)"
        ")"), "create product_components");
    ensureColumn("product_components", "installed_at", "TEXT NOT NULL DEFAULT ''");
    ensureColumn("product_components", "removed_at", "TEXT");
    ensureColumn("product_components", "removal_reason", "TEXT");
    ensureColumn("product_components", "active", "INTEGER NOT NULL DEFAULT 1");

    execOrThrow(database_, QStringLiteral(
        "CREATE TABLE IF NOT EXISTS stage_attempts ("
        "id TEXT PRIMARY KEY, product_id TEXT NOT NULL, component_id TEXT,"
        "stage TEXT NOT NULL, verdict TEXT NOT NULL, opened_at TEXT NOT NULL,"
        "finished_at TEXT, FOREIGN KEY(product_id) REFERENCES products(id),"
        "FOREIGN KEY(component_id) REFERENCES components(id)"
        ")"), "create stage_attempts");
    ensureColumn("stage_attempts", "component_id", "TEXT");
    ensureColumn("stage_attempts", "stage", "TEXT NOT NULL DEFAULT ''");
    ensureColumn("stage_attempts", "verdict", "TEXT NOT NULL DEFAULT 'InProgress'");
    ensureColumn("stage_attempts", "opened_at", "TEXT NOT NULL DEFAULT ''");
    ensureColumn("stage_attempts", "finished_at", "TEXT");

    execOrThrow(database_, QStringLiteral(
        "CREATE TABLE IF NOT EXISTS stage_runs ("
        "stage_attempt_id TEXT PRIMARY KEY, run_id TEXT NOT NULL UNIQUE,"
        "FOREIGN KEY(stage_attempt_id) REFERENCES stage_attempts(id)"
        ")"), "create stage_runs");

    execOrThrow(database_, QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_product_components_active "
        "ON product_components(product_id, active)"), "index product_components");
    execOrThrow(database_, QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_stage_attempts_product "
        "ON stage_attempts(product_id, component_id, stage)"), "index stage_attempts");
}

void Registrar::ensureColumn(
    const char* table,
    const char* column,
    const char* definition)
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        throwSql(query, "inspect schema");
    }
    while (query.next()) {
        if (query.value(1).toString() == QString::fromUtf8(column)) return;
    }
    const QString alter = QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
        .arg(table, column, definition);
    if (!query.exec(alter)) throwSql(query, "migrate schema");
}

void Registrar::ensureProduct(const std::string& productId) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT 1 FROM products WHERE id = ?"));
    query.addBindValue(QString::fromStdString(productId));
    if (!query.exec()) throwSql(query, "find product");
    if (!query.next()) throw std::invalid_argument("product not found: " + productId);
}

void Registrar::ensureComponent(const std::string& componentId) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT 1 FROM components WHERE id = ?"));
    query.addBindValue(QString::fromStdString(componentId));
    if (!query.exec()) throwSql(query, "find component");
    if (!query.next()) throw std::invalid_argument("component not found: " + componentId);
}

void Registrar::ensureActiveBinding(
    const std::string& productId,
    const std::string& componentId) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM product_components WHERE product_id = ? "
        "AND component_id = ? AND active = 1"));
    query.addBindValue(QString::fromStdString(productId));
    query.addBindValue(QString::fromStdString(componentId));
    if (!query.exec()) throwSql(query, "find active component binding");
    if (!query.next()) throw std::invalid_argument("component is not installed in product");
}

std::string Registrar::createProduct(
    const std::string& productType,
    const std::string& serialNumber)
{
    if (productType.empty() || serialNumber.empty()) {
        throw std::invalid_argument("product type and serial number are required");
    }
    const std::string id = makeId();
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO products(id, product_type, serial_number, created_at) "
        "VALUES(?, ?, ?, ?)"));
    query.addBindValue(QString::fromStdString(id));
    query.addBindValue(QString::fromStdString(productType));
    query.addBindValue(QString::fromStdString(serialNumber));
    query.addBindValue(nowUtc());
    if (!query.exec()) throwSql(query, "create product");
    return id;
}

std::string Registrar::createComponent(
    const std::string& componentType,
    const std::string& serialNumber)
{
    if (componentType.empty() || serialNumber.empty()) {
        throw std::invalid_argument("component type and serial number are required");
    }
    const std::string id = makeId();
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO components(id, component_type, serial_number, created_at) "
        "VALUES(?, ?, ?, ?)"));
    query.addBindValue(QString::fromStdString(id));
    query.addBindValue(QString::fromStdString(componentType));
    query.addBindValue(QString::fromStdString(serialNumber));
    query.addBindValue(nowUtc());
    if (!query.exec()) throwSql(query, "create component");
    return id;
}

void Registrar::installComponent(
    const std::string& productId,
    const std::string& componentId)
{
    ensureProduct(productId);
    ensureComponent(componentId);

    QSqlQuery typeQuery(database_);
    typeQuery.prepare(QStringLiteral("SELECT component_type FROM components WHERE id = ?"));
    typeQuery.addBindValue(QString::fromStdString(componentId));
    if (!typeQuery.exec()) throwSql(typeQuery, "read component type");
    if (!typeQuery.next()) throw std::invalid_argument("component not found");

    QSqlQuery activeQuery(database_);
    activeQuery.prepare(QStringLiteral(
        "SELECT 1 FROM product_components pc JOIN components c ON c.id = pc.component_id "
        "WHERE pc.product_id = ? AND c.component_type = ? AND pc.active = 1 LIMIT 1"));
    activeQuery.addBindValue(QString::fromStdString(productId));
    activeQuery.addBindValue(typeQuery.value(0));
    if (!activeQuery.exec()) throwSql(activeQuery, "check active component");
    if (activeQuery.next()) {
        throw std::logic_error(
            "an active component of this type is already installed; remove it first");
    }

    if (!database_.transaction()) throw std::runtime_error("begin install transaction failed");
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO product_components "
        "(id, product_id, component_id, installed_at, active) VALUES(?, ?, ?, ?, 1)"));
    query.addBindValue(QString::fromStdString(makeId()));
    query.addBindValue(QString::fromStdString(productId));
    query.addBindValue(QString::fromStdString(componentId));
    query.addBindValue(nowUtc());
    if (!query.exec()) {
        database_.rollback();
        throwSql(query, "install component");
    }
    if (!database_.commit()) throw std::runtime_error("commit install transaction failed");
}

void Registrar::removeComponent(
    const std::string& productId,
    const std::string& componentId,
    const std::string& reason)
{
    ensureProduct(productId);
    ensureComponent(componentId);
    ensureActiveBinding(productId, componentId);
    if (reason.empty()) throw std::invalid_argument("removal reason is required");

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE product_components SET active = 0, removed_at = ?, removal_reason = ? "
        "WHERE product_id = ? AND component_id = ? AND active = 1"));
    query.addBindValue(nowUtc());
    query.addBindValue(QString::fromStdString(reason));
    query.addBindValue(QString::fromStdString(productId));
    query.addBindValue(QString::fromStdString(componentId));
    if (!query.exec()) throwSql(query, "remove component");
    if (query.numRowsAffected() != 1) throw std::logic_error("component was removed concurrently");
}

std::string Registrar::beginStageInternal(
    const std::string& productId,
    const std::string& componentId,
    Stage stage)
{
    ensureProduct(productId);
    if (!componentId.empty()) {
        ensureComponent(componentId);
        ensureActiveBinding(productId, componentId);
    }

    QSqlQuery duplicate(database_);
    duplicate.prepare(QStringLiteral(
        "SELECT 1 FROM stage_attempts WHERE product_id = ? AND stage = ? "
        "AND verdict = 'InProgress' "
        "AND ((component_id IS NULL AND ? = '') OR component_id = ?) LIMIT 1"));
    duplicate.addBindValue(QString::fromStdString(productId));
    duplicate.addBindValue(stageValue(stage));
    duplicate.addBindValue(QString::fromStdString(componentId));
    duplicate.addBindValue(QString::fromStdString(componentId));
    if (!duplicate.exec()) throwSql(duplicate, "check open stage");
    if (duplicate.next()) throw std::logic_error("stage already in progress");

    const std::string id = makeId();
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO stage_attempts "
        "(id, product_id, component_id, stage, verdict, opened_at) "
        "VALUES(?, ?, NULLIF(?, ''), ?, 'InProgress', ?)"));
    query.addBindValue(QString::fromStdString(id));
    query.addBindValue(QString::fromStdString(productId));
    query.addBindValue(QString::fromStdString(componentId));
    query.addBindValue(stageValue(stage));
    query.addBindValue(nowUtc());
    if (!query.exec()) throwSql(query, "begin stage");
    return id;
}

std::string Registrar::beginStage(const std::string& productId, Stage stage)
{
    return beginStageInternal(productId, {}, stage);
}

std::string Registrar::beginComponentStage(
    const std::string& productId,
    const std::string& componentId,
    Stage stage)
{
    if (componentId.empty()) throw std::invalid_argument("component id is required");
    return beginStageInternal(productId, componentId, stage);
}

void Registrar::attachRun(const std::string& stageAttemptId, const std::string& runId)
{
    if (stageAttemptId.empty() || runId.empty()) {
        throw std::invalid_argument("stage attempt id and run id are required");
    }
    QSqlQuery attempt(database_);
    attempt.prepare(QStringLiteral("SELECT 1 FROM stage_attempts WHERE id = ?"));
    attempt.addBindValue(QString::fromStdString(stageAttemptId));
    if (!attempt.exec()) throwSql(attempt, "find stage attempt");
    if (!attempt.next()) throw std::invalid_argument("stage attempt not found");

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO stage_runs(stage_attempt_id, run_id) VALUES(?, ?)"));
    query.addBindValue(QString::fromStdString(stageAttemptId));
    query.addBindValue(QString::fromStdString(runId));
    if (!query.exec()) throwSql(query, "attach run");
}

void Registrar::finishStage(const std::string& stageAttemptId, Verdict verdict)
{
    if (verdict == Verdict::InProgress || verdict == Verdict::Incomplete) {
        throw std::invalid_argument("a finished stage must have a terminal verdict");
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE stage_attempts SET verdict = ?, finished_at = ? "
        "WHERE id = ? AND verdict = 'InProgress'"));
    query.addBindValue(verdictValue(verdict));
    query.addBindValue(nowUtc());
    query.addBindValue(QString::fromStdString(stageAttemptId));
    if (!query.exec()) throwSql(query, "finish stage");
    if (query.numRowsAffected() != 1) {
        throw std::logic_error("stage attempt not found or already finished");
    }
}

std::vector<Product> Registrar::listProducts() const
{
    std::vector<Product> products;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT id, product_type, serial_number FROM products ORDER BY created_at, id"))) {
        throwSql(query, "list products");
    }
    while (query.next()) {
        products.push_back({query.value(0).toString().toStdString(),
                            query.value(1).toString().toStdString(),
                            query.value(2).toString().toStdString()});
    }
    return products;
}

std::vector<Component> Registrar::listComponents() const
{
    std::vector<Component> components;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT id, component_type, serial_number FROM components ORDER BY created_at, id"))) {
        throwSql(query, "list components");
    }
    while (query.next()) {
        components.push_back({query.value(0).toString().toStdString(),
                              query.value(1).toString().toStdString(),
                              query.value(2).toString().toStdString()});
    }
    return components;
}

std::vector<ComponentBinding> Registrar::listInstalledComponents(
    const std::string& productId) const
{
    ensureProduct(productId);
    std::vector<ComponentBinding> bindings;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT pc.product_id, pc.component_id, c.component_type, c.serial_number, "
        "pc.active, pc.installed_at, COALESCE(pc.removed_at, ''), "
        "COALESCE(pc.removal_reason, '') FROM product_components pc "
        "JOIN components c ON c.id = pc.component_id WHERE pc.product_id = ? "
        "ORDER BY pc.installed_at, pc.id"));
    query.addBindValue(QString::fromStdString(productId));
    if (!query.exec()) throwSql(query, "list product components");
    while (query.next()) {
        bindings.push_back({query.value(0).toString().toStdString(),
                            query.value(1).toString().toStdString(),
                            query.value(2).toString().toStdString(),
                            query.value(3).toString().toStdString(),
                            query.value(4).toInt() != 0,
                            query.value(5).toString().toStdString(),
                            query.value(6).toString().toStdString(),
                            query.value(7).toString().toStdString()});
    }
    return bindings;
}

std::vector<StageAttempt> Registrar::listStageAttempts(const std::string& productId) const
{
    ensureProduct(productId);
    std::vector<StageAttempt> attempts;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT sa.id, sa.product_id, COALESCE(sa.component_id, ''), sa.stage, "
        "sa.verdict, sa.opened_at, COALESCE(sa.finished_at, ''), COALESCE(sr.run_id, '') "
        "FROM stage_attempts sa LEFT JOIN stage_runs sr ON sr.stage_attempt_id = sa.id "
        "WHERE sa.product_id = ? ORDER BY sa.opened_at, sa.id"));
    query.addBindValue(QString::fromStdString(productId));
    if (!query.exec()) throwSql(query, "list stage attempts");
    while (query.next()) {
        attempts.push_back({query.value(0).toString().toStdString(),
                            query.value(1).toString().toStdString(),
                            query.value(2).toString().toStdString(),
                            stageFromString(query.value(3).toString().toStdString()),
                            verdictFromString(query.value(4).toString().toStdString()),
                            query.value(5).toString().toStdString(),
                            query.value(6).toString().toStdString(),
                            query.value(7).toString().toStdString()});
    }
    return attempts;
}

Verdict Registrar::productVerdict(const std::string& productId) const
{
    ensureProduct(productId);
    const auto bindings = listInstalledComponents(productId);
    if (bindings.empty()) return Verdict::Incomplete;

    const std::array<Stage, 4> requiredStages = {
        Stage::InitialElectrical, Stage::PostVibrationElectrical,
        Stage::PostClimateElectrical, Stage::FinalElectrical};
    bool incomplete = false;
    for (const auto& binding : bindings) {
        if (!binding.active) continue;
        for (const Stage stage : requiredStages) {
            QSqlQuery query(database_);
            query.prepare(QStringLiteral(
                "SELECT verdict FROM stage_attempts WHERE product_id = ? "
                "AND component_id = ? AND stage = ? ORDER BY opened_at DESC LIMIT 1"));
            query.addBindValue(QString::fromStdString(productId));
            query.addBindValue(QString::fromStdString(binding.componentId));
            query.addBindValue(stageValue(stage));
            if (!query.exec()) throwSql(query, "calculate product verdict");
            if (!query.next()) {
                // Совместимость с прежним агрегатным API: блоковый этап может
                // быть принят как общий для всех активных ячеек, если для этой
                // ячейки ещё нет отдельной попытки.
                QSqlQuery aggregate(database_);
                aggregate.prepare(QStringLiteral(
                    "SELECT verdict FROM stage_attempts WHERE product_id = ? "
                    "AND component_id IS NULL AND stage = ? "
                    "ORDER BY opened_at DESC LIMIT 1"));
                aggregate.addBindValue(QString::fromStdString(productId));
                aggregate.addBindValue(stageValue(stage));
                if (!aggregate.exec()) throwSql(aggregate, "calculate aggregate verdict");
                if (!aggregate.next()) {
                    incomplete = true;
                    continue;
                }
                query = std::move(aggregate);
            }
            const Verdict verdict = verdictFromString(query.value(0).toString().toStdString());
            if (verdict == Verdict::Fail) return Verdict::Fail;
            if (verdict != Verdict::Ok) incomplete = true;
        }
    }
    return incomplete ? Verdict::Incomplete : Verdict::Ok;
}

ProductReport Registrar::productReport(const std::string& productId) const
{
    ensureProduct(productId);
    const auto products = listProducts();
    const auto product = std::find_if(products.begin(), products.end(),
        [&productId](const Product& value) { return value.id == productId; });
    if (product == products.end()) throw std::invalid_argument("product not found: " + productId);

    ProductReport report;
    report.product = *product;
    report.components = listInstalledComponents(productId);
    report.stageAttempts = listStageAttempts(productId);
    report.verdict = productVerdict(productId);
    return report;
}

} // namespace ktma::registrar
