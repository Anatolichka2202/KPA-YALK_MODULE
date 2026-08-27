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

// Конкретная привязка одного логического канала. Сценарий работает с
// parameterGroup/channelIndex; источник и физический локатор берутся отсюда.
struct CatalogParameterBinding {
    std::string blockType;
    std::string slotId;
    std::string parameterGroup;
    unsigned channelIndex = 0;
    std::string source;
    std::string locatorType;
    std::string locator;
    unsigned mask = 0xFFFF;
    unsigned mode = 0;
    // Семантический маршрут воздействия. Физический базовый канал этого
    // маршрута назначается в профиле конкретного стенда.
    std::string stimulusRoute;
    unsigned stimulusOffset = 0;
    bool confirmed = false;
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

CatalogParameterBinding resolveCatalogParameterBinding(
    const std::string& sqlitePath, const std::string& blockType,
    const std::string& parameterGroup, unsigned channelIndex);

} // namespace orbita::stand
