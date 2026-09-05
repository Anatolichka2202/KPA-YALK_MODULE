#include "registrar.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QUuid>

#include <stdexcept>

namespace ktma::registrar {

Registrar::Registrar(const std::string& databasePath)
{
    database_ = QSqlDatabase::addDatabase("QSQLITE");

    database_.setDatabaseName(
        QString::fromStdString(databasePath)
        );

    if (!database_.open()) {
        throw std::runtime_error(
            database_.lastError().text().toStdString()
            );
    }

    QSqlQuery query(database_);

    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS products ("
            "id TEXT PRIMARY KEY,"
            "product_type TEXT NOT NULL,"
            "serial_number TEXT NOT NULL UNIQUE"
            ")"
            )){
        throw std::runtime_error(
            query.lastError().text().toStdString()
            );
    }

    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS components("
            "id TEXT PRIMARY KEY,"
            "component_type TEXT NOT NULL,"
            "serial_number TEXT NOT NULL,"
            "UNIQUE(component_type, serial_number)"
            ")"
            )){
        throw std::runtime_error(
            query.lastError().text().toStdString()
            );
    }
}

std::string Registrar::createProduct(
    const std::string& productType,
    const std::string& serialNumber)
{
    const QString id =
        QUuid::createUuid().toString(QUuid::WithoutBraces);

    QSqlQuery query(database_);

    query.prepare(
        "INSERT INTO products "
        "(id, product_type, serial_number) "
        "VALUES (?, ?, ?)"
        );

    query.addBindValue(id);
    query.addBindValue(QString::fromStdString(productType));
    query.addBindValue(QString::fromStdString(serialNumber));

    if (!query.exec()) {
        throw std::runtime_error(
            query.lastError().text().toStdString()
            );
    }

    return id.toStdString();
}

std::string Registrar::createComponent(
    const std::string& componentType,
    const std::string& serialNumber)
{
    const QString id =
        QUuid::createUuid().toString(QUuid::WithoutBraces);

    QSqlQuery query(database_);

    query.prepare(
        "INSERT INTO components "
        "(id, component_type, serial_number) "
        "VALUES(?, ?, ?)"
        );

    query.addBindValue(id);
    query.addBindValue(QString::fromStdString(componentType));
    query.addBindValue(QString::fromStdString(serialNumber));

    if(!query.exec()) {
        throw std::runtime_error(
            query.lastError().text().toStdString()
            );
    }

    return id.toStdString();
}

std::vector<Product> Registrar::listProducts()
{
    std::vector<Product> products;

    QSqlQuery query(database_);

    if (!query.exec(
            "SELECT id, product_type, serial_number "
            "FROM products"
            )) {
        throw std::runtime_error(
            query.lastError().text().toStdString()
            );
    }

    while (query.next()) {
        Product product;

        product.id =
            query.value(0).toString().toStdString();

        product.productType =
            query.value(1).toString().toStdString();

        product.serialNumber =
            query.value(2).toString().toStdString();

        products.push_back(product);
    }

    return products;
}

} // namespace ktma::registrar
