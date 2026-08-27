#include "orbita_stand/catalog.h"

#include <QCoreApplication>

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 3) {
        std::cerr << "Usage: catalog_import <catalog.yaml> <parameters.db>\n";
        return EXIT_FAILURE;
    }
    try {
        const auto catalog = orbita::stand::importCatalogYaml(argv[1], argv[2]);
        std::cout << "Imported catalog " << catalog.version << ": "
                  << catalog.blockTypes.size() << " block types, "
                  << catalog.cellTypes.size() << " cell types, "
                  << catalog.instances.size() << " instances\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Catalog import failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
