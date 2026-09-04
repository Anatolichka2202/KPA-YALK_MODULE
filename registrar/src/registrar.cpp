#include "registrar.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QString>

#include <stdexcept>

namespace ktma::registrar {
Registrar::Registrar(const std::string& databasePatch)
{
    database_ = QSqlDatabase::addDatabase("SQLITE");

    database_.setDatabaseName(
        QString::fromStdString(databasePatch)
        );

    if(!database_.open())
    {
        throw std::runtime_error(
            database_.lastError().text().toStdString()
            );
    }

    QSqlQuery query(database_);

    if(!query.exec(
            "CREATE TABLE IF NOT EXISTS products ("
            "id TEXT PRIMARY KEY,"
            "product_type TEXT NOT NULL,"
            "serial_number TEXT NOT NULL UNIQUE"
            ")"
            )) {
        throw::std::runtime_error(
            query.lastError().text().toStdString()
            );
    }
}
}   //namespace ktma::registrar
