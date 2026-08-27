#pragma once

#include <string>
#include <vector>

namespace orbita::stand {

struct CatalogCellType {
    std::string id;
    std::string name;
};

struct CatalogBlockSlot {
    std::string id;
    std::string cellType;
    unsigned order = 0;
};

struct CatalogBlockType {
    std::string id;
    std::string name;
    std::string designation;
    std::vector<CatalogBlockSlot> cellSlots;
};

struct CatalogBlockInstance {
    std::string id;
    std::string blockType;
    std::string name;
    std::string serial;
};

struct StandCatalog {
    std::string version;
    std::string title;
    std::vector<CatalogCellType> cellTypes;
    std::vector<CatalogBlockType> blockTypes;
    std::vector<CatalogBlockInstance> instances;
};

// Импортирует версионируемый YAML в отдельные нормализованные таблицы SQLite.
// Старые parameters/addresses не изменяются.
StandCatalog importCatalogYaml(const std::string& yamlPath, const std::string& sqlitePath);
StandCatalog loadCatalog(const std::string& sqlitePath);

} // namespace orbita::stand
