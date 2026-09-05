#include "registrar.h"

#include <QCoreApplication>

#include <iostream>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    try {
        ktma::registrar::Registrar registrar("registrar.db");

        registrar.createComponent("YALK-96", "YALK-0042");
        registrar.createComponent("YTP", "YTP-0017");

        const auto products = registrar.listProducts();

        for (const auto& product : products) {
            std::cout
                << product.id << " | "
                << product.productType << " | "
                << product.serialNumber
                << '\n';
        }
    }
    catch (const std::exception& error) {
        std::cerr
            << "ERROR: "
            << error.what()
            << '\n';

        return 1;
    }

    return 0;
}
