#include "parameter_database.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QCoreApplication>
#include <QDebug>
#include "encoding_utils.h"

ParameterDatabase::ParameterDatabase(QObject *parent)
    : QObject(parent)
    , db_(std::make_unique<QSqlDatabase>())
{
}

ParameterDatabase::~ParameterDatabase() = default;

bool ParameterDatabase::open(const QString& dbPath) {
    QString path = dbPath.isEmpty()
    ? QCoreApplication::applicationDirPath() + "/parameters.db"
    : dbPath;
    *db_ = QSqlDatabase::addDatabase("QSQLITE");
    db_->setDatabaseName(path);
    if (!db_->open()) {
        qWarning() << "Failed to open database:" << db_->lastError().text();
        return false;
    }
    loadMetadata();
    return true;
}

void ParameterDatabase::refresh() {
    loadMetadata();
}

void ParameterDatabase::loadMetadata() {
    nameMap_.clear();
    categoryMap_.clear();
    if (!db_->isOpen()) return;

    QSqlQuery query("SELECT \"Полный адрес\", Параметр, Категория FROM parameters "
                    "WHERE \"Полный адрес\" IS NOT NULL AND TRIM(\"Полный адрес\") != ''");
    while (query.next()) {
        QString rawAddr = query.value(0).toString();
        QString name = query.value(1).toString();
        QString category = query.value(2).toString();
        std::string normUtf8 = encoding::normalizeAddress(rawAddr.toStdString());
        QString normAddr = QString::fromUtf8(normUtf8.c_str());
        nameMap_[normAddr] = name;
        categoryMap_[normAddr] = category;
    }
    qDebug() << "ParameterDatabase: loaded" << nameMap_.size() << "entries";
}

QString ParameterDatabase::getName(const QString& normalizedAddress) const {
    return nameMap_.value(normalizedAddress, QString());
}

QString ParameterDatabase::getCategory(const QString& normalizedAddress) const {
    return categoryMap_.value(normalizedAddress, QString());
}

QStringList ParameterDatabase::getAllCategories() const {
    QSet<QString> cats;
    for (auto it = categoryMap_.begin(); it != categoryMap_.end(); ++it)
        if (!it.value().isEmpty()) cats.insert(it.value());
    return cats.values();
}

QHash<QString, QString> ParameterDatabase::getParametersByCategory(const QString& category) const {
    QHash<QString, QString> result;
    for (auto it = nameMap_.begin(); it != nameMap_.end(); ++it) {
        if (categoryMap_.value(it.key()) == category) {
            result[it.key()] = it.value();
        }
    }
    return result;
}
