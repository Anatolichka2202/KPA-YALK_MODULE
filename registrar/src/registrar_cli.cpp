#include "registrar.h"

#include <QCoreApplication>

#include <iostream>


#include <QDebug>
#include <QSqlDatabase>


int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "Qt plugin paths:";
    qDebug() << QCoreApplication::libraryPaths();

    qDebug() << "SQL drivers:";
    qDebug() << QSqlDatabase::drivers();

    try {
        ktma::registrar::Registrar registrar("registrar.db");

        std::cout << "Database opened successfully\n";
    }
    catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
