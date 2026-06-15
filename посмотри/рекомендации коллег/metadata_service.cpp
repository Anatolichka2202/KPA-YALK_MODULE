#include "metadata_service.h"

#include <QCoreApplication>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QVariant>
#include <QDebug>
#include <QUuid>

// ─────────────────────────────────────────────────────────────────────────────
// normalize — статический метод, используется везде где нужно сравнить адреса
// ─────────────────────────────────────────────────────────────────────────────
/*static*/
QString MetadataService::normalize(const QString& raw) {
    QString s = raw.simplified();               // убираем лишние пробелы
    s.replace(QChar(0x041F), QChar('P'));        // П (кирилл.) → P
    s.replace(QChar(0x041C), QChar('M'));        // М (кирилл.) → M
    s.replace(QLatin1Char(' '), QString());      // убираем оставшиеся пробелы
    return s.toUpper();
}

// ─────────────────────────────────────────────────────────────────────────────
MetadataService::MetadataService(QObject* parent)
    : QObject(parent)
    // Уникальное имя соединения: несколько экземпляров в тестах не конфликтуют
    , connectionName_(QStringLiteral("orbita_meta_") + QUuid::createUuid().toString())
{}

bool MetadataService::open() {
    const QString path =
        QCoreApplication::applicationDirPath() + QLatin1String("/address/parameters.db");
    return openPath(path);
}

bool MetadataService::openPath(const QString& dbPath) {
    if (QSqlDatabase::contains(connectionName_))
        QSqlDatabase::removeDatabase(connectionName_);

    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    db_.setDatabaseName(dbPath);

    if (!db_.open()) {
        const QString msg = tr("Ошибка открытия БД '%1': %2")
                                .arg(dbPath, db_.lastError().text());
        emit loadError(msg);
        qWarning() << msg;
        return false;
    }

    if (!ensureSchema()) {
        emit loadError(tr("Схема БД не соответствует ожидаемой: %1").arg(dbPath));
        return false;
    }

    qDebug() << "MetadataService: opened" << dbPath;
    return true;
}

bool MetadataService::isOpen() const {
    return db_.isOpen();
}

// Минимальная проверка что нужная таблица существует
bool MetadataService::ensureSchema() const {
    QSqlQuery q(db_);
    return q.exec(QStringLiteral(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name='parameters' LIMIT 1"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Вспомогательный метод: выполняет SELECT и возвращает первые два столбца
// как список пар {col0, col1}
// ─────────────────────────────────────────────────────────────────────────────
QList<QPair<QString,QString>>
MetadataService::queryPairs(const QString& sql, const QVariantList& binds) const
{
    QList<QPair<QString,QString>> result;
    if (!isOpen()) return result;

    QSqlQuery q(db_);
    q.prepare(sql);
    for (int i = 0; i < binds.size(); ++i) q.addBindValue(binds[i]);

    if (!q.exec()) {
        qWarning() << "MetadataService query error:" << q.lastError().text();
        return result;
    }
    while (q.next())
        result.append({q.value(0).toString(), q.value(1).toString()});
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
std::optional<ParamInfo> MetadataService::lookup(const QString& rawAddress) const {
    if (!isOpen()) return std::nullopt;

    const QString addr = normalize(rawAddress);

    // Колонка "Полный адрес" в оригинальной xlsx — адреса там тоже на кириллице.
    // В parameters.db они должны быть уже нормализованы при импорте из xlsx.
    // Пробуем оба варианта: нормализованный и исходный.
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "SELECT \"Параметр\", \"Категория\" "
        "FROM parameters "
        "WHERE \"Полный адрес\" = ? OR \"Полный адрес\" = ? "
        "LIMIT 1"));
    q.addBindValue(addr);
    q.addBindValue(rawAddress);

    if (!q.exec() || !q.next()) return std::nullopt;

    ParamInfo info;
    info.name     = q.value(0).toString();
    info.category = q.value(1).toString();
    return info;
}

QStringList MetadataService::categories() const {
    QStringList result;
    if (!isOpen()) return result;
    QSqlQuery q(QStringLiteral(
        "SELECT DISTINCT \"Категория\" FROM parameters "
        "WHERE \"Категория\" IS NOT NULL AND \"Категория\" != '' "
        "ORDER BY \"Категория\""), db_);
    while (q.next()) result << q.value(0).toString();
    return result;
}

QList<QPair<QString,QString>>
MetadataService::paramsByCategory(const QString& category) const {
    return queryPairs(
        QStringLiteral(
            "SELECT \"Полный адрес\", \"Параметр\" FROM parameters "
            "WHERE \"Категория\" = ? AND \"Полный адрес\" IS NOT NULL "
            "AND TRIM(\"Полный адрес\") != '' "
            "ORDER BY \"Параметр\""),
        {category});
}

QList<QPair<QString,QString>> MetadataService::allParams() const {
    return queryPairs(
        QStringLiteral(
            "SELECT \"Полный адрес\", \"Параметр\" FROM parameters "
            "WHERE \"Полный адрес\" IS NOT NULL AND TRIM(\"Полный адрес\") != '' "
            "ORDER BY \"Параметр\""));
}

QHash<QString,QString> MetadataService::addressToName() const {
    QHash<QString,QString> h;
    for (const auto& [addr, name] : allParams())
        h[normalize(addr)] = name;
    return h;
}

QHash<QString,QString> MetadataService::addressToCategory() const {
    QHash<QString,QString> h;
    if (!isOpen()) return h;
    QSqlQuery q(QStringLiteral(
        "SELECT \"Полный адрес\", \"Категория\" FROM parameters "
        "WHERE \"Полный адрес\" IS NOT NULL AND TRIM(\"Полный адрес\") != ''"), db_);
    while (q.next())
        h[normalize(q.value(0).toString())] = q.value(1).toString();
    return h;
}
