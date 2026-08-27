#include "orbita_stand/catalog.h"
#include "orbita_stand/yaml_lite.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QVariant>

#include <atomic>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace orbita::stand {
namespace {

std::atomic_uint connectionCounter{0};

std::string required(const yaml::Node& node, const std::string& key)
{
    const auto value = node.value(key);
    if (value.empty() && key != "serial") throw yaml::Error("Missing catalog key: " + key);
    return value;
}

unsigned unsignedValue(const yaml::Node& node, const std::string& key)
{
    const auto value = required(node, key);
    std::size_t parsed = 0;
    const auto result = std::stoul(value, &parsed);
    if (parsed != value.size()) throw yaml::Error("Invalid catalog number: " + key);
    return static_cast<unsigned>(result);
}

unsigned optionalUnsignedValue(
    const yaml::Node& node, const std::string& key, unsigned fallback)
{
    const auto value = node.value(key);
    if (value.empty()) return fallback;
    std::size_t parsed = 0;
    const auto result = std::stoul(value, &parsed, 0);
    if (parsed != value.size()) throw yaml::Error("Invalid catalog number: " + key);
    return static_cast<unsigned>(result);
}

bool booleanValue(const yaml::Node& node, const std::string& key, bool fallback)
{
    auto value = node.value(key);
    if (value.empty()) return fallback;
    for (auto& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    if (value == "true" || value == "yes" || value == "1") return true;
    if (value == "false" || value == "no" || value == "0") return false;
    throw yaml::Error("Invalid catalog boolean: " + key);
}

std::string normalizeLegacyAddress(std::string line)
{
    const auto end = line.find_first_of(" \t\r\n");
    if (end != std::string::npos) line.resize(end);
    for (char& character : line) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte == 0xCC) character = 'M';       // М, Windows-1251
        else if (byte == 0xCF) character = 'P';  // П, Windows-1251
        else character = static_cast<char>(std::toupper(byte));
    }
    return line;
}

std::vector<std::string> loadAddressFile(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw yaml::Error("Cannot open catalog address file: " + path);
    std::vector<std::string> result;
    for (std::string line; std::getline(input, line);) {
        auto address = normalizeLegacyAddress(std::move(line));
        if (!address.empty()) result.push_back(std::move(address));
    }
    return result;
}

void requireExec(QSqlQuery& query, const QString& sql)
{
    if (!query.exec(sql)) throw std::runtime_error(query.lastError().text().toUtf8().toStdString());
}

void requirePrepared(QSqlQuery& query)
{
    if (!query.exec()) throw std::runtime_error(query.lastError().text().toUtf8().toStdString());
}

struct Connection {
    QString name;
    QSqlDatabase database;
    explicit Connection(const std::string& path)
    {
        name = QStringLiteral("orbita_catalog_%1").arg(++connectionCounter);
        database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        database.setDatabaseName(QString::fromUtf8(path));
        if (!database.open()) throw std::runtime_error(database.lastError().text().toUtf8().toStdString());
    }
    ~Connection()
    {
        database.close();
        database = {};
        QSqlDatabase::removeDatabase(name);
    }
};

StandCatalog parseCatalog(const yaml::Node& root)
{
    if (!root.isMap() || root.value("schema") != "1") throw yaml::Error("Unsupported catalog schema");
    StandCatalog catalog;
    catalog.version = required(root, "version");
    catalog.title = required(root, "title");
    std::set<std::string> cells;
    const auto& cellTypes = root.at("cell_types");
    if (!cellTypes.isSequence()) throw yaml::Error("cell_types must be a sequence");
    for (const auto& value : cellTypes.sequence) {
        CatalogCellType cell{required(value, "id"), required(value, "name")};
        if (!cells.insert(cell.id).second) throw yaml::Error("Duplicate cell type: " + cell.id);
        catalog.cellTypes.push_back(std::move(cell));
    }
    std::set<std::string> blocks;
    const auto& blockTypes = root.at("block_types");
    if (!blockTypes.isSequence()) throw yaml::Error("block_types must be a sequence");
    for (const auto& value : blockTypes.sequence) {
        CatalogBlockType block{required(value, "id"), required(value, "name"), required(value, "designation"), {}};
        if (!blocks.insert(block.id).second) throw yaml::Error("Duplicate block type: " + block.id);
        const auto& slotNodes = value.at("slots");
        if (!slotNodes.isSequence()) throw yaml::Error("Block slots must be a sequence");
        std::set<std::string> slotIds;
        for (const auto& slotValue : slotNodes.sequence) {
            CatalogBlockSlot slot{required(slotValue, "id"), required(slotValue, "cell_type"),
                                  unsignedValue(slotValue, "order")};
            if (!cells.count(slot.cellType)) throw yaml::Error("Unknown cell type " + slot.cellType);
            if (!slotIds.insert(slot.id).second) throw yaml::Error("Duplicate slot " + slot.id + " in " + block.id);
            block.cellSlots.push_back(std::move(slot));
        }
        catalog.blockTypes.push_back(std::move(block));
    }
    if (const auto* instances = root.find("instances")) {
        if (!instances->isSequence()) throw yaml::Error("instances must be a sequence");
        for (const auto& value : instances->sequence) {
            CatalogBlockInstance instance{required(value, "id"), required(value, "block_type"),
                                          required(value, "name"), value.value("serial")};
            if (!blocks.count(instance.blockType)) throw yaml::Error("Unknown instance block type " + instance.blockType);
            catalog.instances.push_back(std::move(instance));
        }
    }
    return catalog;
}

} // namespace

StandCatalog importCatalogYaml(const std::string& yamlPath, const std::string& sqlitePath)
{
    const auto root = yaml::parseFile(yamlPath);
    auto catalog = parseCatalog(root);
    Connection connection(sqlitePath);
    QSqlQuery query(connection.database);
    requireExec(query, QStringLiteral("PRAGMA foreign_keys = ON"));
    for (const auto& sql : {
             "CREATE TABLE IF NOT EXISTS catalog_meta(key TEXT PRIMARY KEY,value TEXT NOT NULL)",
             "CREATE TABLE IF NOT EXISTS catalog_cell_types(id TEXT PRIMARY KEY,name TEXT NOT NULL)",
             "CREATE TABLE IF NOT EXISTS catalog_block_types(id TEXT PRIMARY KEY,name TEXT NOT NULL,designation TEXT NOT NULL)",
             "CREATE TABLE IF NOT EXISTS catalog_block_slots(block_type TEXT NOT NULL REFERENCES catalog_block_types(id) ON DELETE CASCADE,slot_id TEXT NOT NULL,cell_type TEXT NOT NULL REFERENCES catalog_cell_types(id),sort_order INTEGER NOT NULL,PRIMARY KEY(block_type,slot_id))",
             "CREATE TABLE IF NOT EXISTS catalog_parameter_groups(id TEXT PRIMARY KEY,name TEXT NOT NULL,cell_type TEXT NOT NULL REFERENCES catalog_cell_types(id),category TEXT NOT NULL,unit TEXT NOT NULL,parameter_count INTEGER NOT NULL)",
             "CREATE TABLE IF NOT EXISTS catalog_bindings(block_type TEXT NOT NULL,slot_id TEXT NOT NULL,parameter_group TEXT NOT NULL REFERENCES catalog_parameter_groups(id),source TEXT NOT NULL,address_key TEXT NOT NULL,PRIMARY KEY(block_type,slot_id,parameter_group),FOREIGN KEY(block_type,slot_id) REFERENCES catalog_block_slots(block_type,slot_id))",
             "CREATE TABLE IF NOT EXISTS catalog_parameter_bindings(block_type TEXT NOT NULL,slot_id TEXT NOT NULL,parameter_group TEXT NOT NULL REFERENCES catalog_parameter_groups(id),channel_index INTEGER NOT NULL,source TEXT NOT NULL,locator_type TEXT NOT NULL,locator TEXT NOT NULL,mask INTEGER NOT NULL,mode INTEGER NOT NULL,stimulus_route TEXT NOT NULL DEFAULT '',stimulus_offset INTEGER NOT NULL DEFAULT 0,confirmed INTEGER NOT NULL,PRIMARY KEY(block_type,slot_id,parameter_group,channel_index),FOREIGN KEY(block_type,slot_id) REFERENCES catalog_block_slots(block_type,slot_id))",
             "CREATE TABLE IF NOT EXISTS catalog_block_instances(id TEXT PRIMARY KEY,block_type TEXT NOT NULL REFERENCES catalog_block_types(id),name TEXT NOT NULL,serial TEXT NOT NULL DEFAULT '')",
             "CREATE TABLE IF NOT EXISTS catalog_cell_instances(id TEXT PRIMARY KEY,block_instance TEXT NOT NULL REFERENCES catalog_block_instances(id) ON DELETE CASCADE,slot_id TEXT NOT NULL,serial TEXT NOT NULL DEFAULT '')"}) {
        requireExec(query, QString::fromLatin1(sql));
    }
    // parameters.db может быть создан предыдущей версией каталога. Это
    // рабочий индекс, поэтому расширяем схему без изменения старых таблиц
    // параметров liborbita.
    std::set<QString> bindingColumns;
    requireExec(query, QStringLiteral("PRAGMA table_info(catalog_parameter_bindings)"));
    while (query.next()) bindingColumns.insert(query.value(1).toString());
    if (!bindingColumns.count(QStringLiteral("stimulus_route"))) {
        requireExec(query, QStringLiteral(
            "ALTER TABLE catalog_parameter_bindings ADD COLUMN stimulus_route TEXT NOT NULL DEFAULT ''"));
    }
    if (!bindingColumns.count(QStringLiteral("stimulus_offset"))) {
        requireExec(query, QStringLiteral(
            "ALTER TABLE catalog_parameter_bindings ADD COLUMN stimulus_offset INTEGER NOT NULL DEFAULT 0"));
    }
    if (!connection.database.transaction()) throw std::runtime_error("Cannot start catalog transaction");
    try {
        for (const auto& table : {"catalog_cell_instances", "catalog_block_instances", "catalog_parameter_bindings", "catalog_bindings",
                                  "catalog_parameter_groups", "catalog_block_slots", "catalog_block_types",
                                  "catalog_cell_types", "catalog_meta"}) {
            requireExec(query, QStringLiteral("DELETE FROM %1").arg(QString::fromLatin1(table)));
        }
        query.prepare(QStringLiteral("INSERT INTO catalog_meta(key,value) VALUES(?,?)"));
        for (const auto& item : std::vector<std::pair<std::string, std::string>>{
                 {"version", catalog.version}, {"title", catalog.title}}) {
            query.bindValue(0, QString::fromUtf8(item.first)); query.bindValue(1, QString::fromUtf8(item.second)); requirePrepared(query); query.finish();
        }
        query.prepare(QStringLiteral("INSERT INTO catalog_cell_types(id,name) VALUES(?,?)"));
        for (const auto& cell : catalog.cellTypes) {
            query.bindValue(0, QString::fromUtf8(cell.id)); query.bindValue(1, QString::fromUtf8(cell.name)); requirePrepared(query); query.finish();
        }
        query.prepare(QStringLiteral("INSERT INTO catalog_block_types(id,name,designation) VALUES(?,?,?)"));
        for (const auto& block : catalog.blockTypes) {
            query.bindValue(0, QString::fromUtf8(block.id)); query.bindValue(1, QString::fromUtf8(block.name));
            query.bindValue(2, QString::fromUtf8(block.designation)); requirePrepared(query); query.finish();
        }
        query.prepare(QStringLiteral("INSERT INTO catalog_block_slots(block_type,slot_id,cell_type,sort_order) VALUES(?,?,?,?)"));
        for (const auto& block : catalog.blockTypes) for (const auto& slot : block.cellSlots) {
            query.bindValue(0, QString::fromUtf8(block.id)); query.bindValue(1, QString::fromUtf8(slot.id));
            query.bindValue(2, QString::fromUtf8(slot.cellType)); query.bindValue(3, slot.order); requirePrepared(query); query.finish();
        }
        const auto& groups = root.at("parameter_groups");
        if (!groups.isSequence()) throw yaml::Error("parameter_groups must be a sequence");
        query.prepare(QStringLiteral("INSERT INTO catalog_parameter_groups(id,name,cell_type,category,unit,parameter_count) VALUES(?,?,?,?,?,?)"));
        for (const auto& group : groups.sequence) {
            query.bindValue(0, QString::fromUtf8(required(group, "id")));
            query.bindValue(1, QString::fromUtf8(required(group, "name")));
            query.bindValue(2, QString::fromUtf8(required(group, "cell_type")));
            query.bindValue(3, QString::fromUtf8(required(group, "category")));
            query.bindValue(4, QString::fromUtf8(required(group, "unit")));
            query.bindValue(5, unsignedValue(group, "count")); requirePrepared(query); query.finish();
        }
        const auto& bindings = root.at("bindings");
        if (!bindings.isSequence()) throw yaml::Error("bindings must be a sequence");
        query.prepare(QStringLiteral("INSERT INTO catalog_bindings(block_type,slot_id,parameter_group,source,address_key) VALUES(?,?,?,?,?)"));
        for (const auto& binding : bindings.sequence) {
            query.bindValue(0, QString::fromUtf8(required(binding, "block_type")));
            query.bindValue(1, QString::fromUtf8(required(binding, "slot")));
            query.bindValue(2, QString::fromUtf8(required(binding, "parameter_group")));
            query.bindValue(3, QString::fromUtf8(required(binding, "source")));
            query.bindValue(4, QString::fromUtf8(required(binding, "address_key"))); requirePrepared(query); query.finish();
        }
        query.prepare(QStringLiteral("INSERT INTO catalog_parameter_bindings(block_type,slot_id,parameter_group,channel_index,source,locator_type,locator,mask,mode,stimulus_route,stimulus_offset,confirmed) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
        for (const auto& binding : bindings.sequence) {
            const auto blockType = required(binding, "block_type");
            const auto slot = required(binding, "slot");
            const auto group = required(binding, "parameter_group");
            const auto source = required(binding, "source");
            const auto locatorType = required(binding, "locator_type");
            const unsigned count = unsignedValue(binding, "count");
            const unsigned mask = optionalUnsignedValue(binding, "mask", 0xFFFF);
            const unsigned mode = optionalUnsignedValue(binding, "mode", 0);
            const std::string stimulusRoute = binding.value("stimulus_route");
            const unsigned stimulusBase = optionalUnsignedValue(binding, "stimulus_offset_base", 0);
            const bool stimulusFromLocator = booleanValue(
                binding, "stimulus_offset_from_locator", false);
            const bool confirmed = booleanValue(binding, "confirmed", false);
            std::vector<std::string> locators;
            if (locatorType == "word") {
                const unsigned base = optionalUnsignedValue(binding, "locator_base", 0);
                for (unsigned index = 0; index < count; ++index) {
                    locators.push_back(std::to_string(base + index));
                }
            } else if (locatorType == "orbita_address" || locatorType == "ulk_address") {
                const QString yamlDirectory = QFileInfo(QString::fromUtf8(yamlPath)).absolutePath();
                const QString filePath = QDir(yamlDirectory).filePath(
                    QString::fromUtf8(required(binding, "address_file")));
                const auto addresses = loadAddressFile(filePath.toUtf8().toStdString());
                const unsigned offset = optionalUnsignedValue(binding, "address_offset", 0);
                if (offset + count > addresses.size()) {
                    throw yaml::Error("Not enough addresses for parameter group " + group);
                }
                locators.assign(addresses.begin() + offset, addresses.begin() + offset + count);
            } else if (locatorType == "unresolved") {
                locators.assign(count, std::string{});
            } else {
                throw yaml::Error("Unsupported locator_type for " + group + ": " + locatorType);
            }
            for (unsigned index = 0; index < count; ++index) {
                query.bindValue(0, QString::fromUtf8(blockType));
                query.bindValue(1, QString::fromUtf8(slot));
                query.bindValue(2, QString::fromUtf8(group));
                query.bindValue(3, index);
                query.bindValue(4, QString::fromUtf8(source));
                query.bindValue(5, QString::fromUtf8(locatorType));
                query.bindValue(6, QString::fromUtf8(locators[index]));
                query.bindValue(7, mask);
                query.bindValue(8, mode);
                query.bindValue(9, QString::fromUtf8(stimulusRoute));
                const unsigned stimulusOffset = stimulusFromLocator
                    ? static_cast<unsigned>(std::stoul(locators[index])) - 1
                    : stimulusBase + index;
                query.bindValue(10, stimulusOffset);
                query.bindValue(11, confirmed ? 1 : 0);
                requirePrepared(query);
                query.finish();
            }
        }
        query.prepare(QStringLiteral("INSERT INTO catalog_block_instances(id,block_type,name,serial) VALUES(?,?,?,?)"));
        for (const auto& instance : catalog.instances) {
            query.bindValue(0, QString::fromUtf8(instance.id)); query.bindValue(1, QString::fromUtf8(instance.blockType));
            query.bindValue(2, QString::fromUtf8(instance.name)); query.bindValue(3, QString::fromUtf8(instance.serial));
            requirePrepared(query); query.finish();
        }
        if (!connection.database.commit()) throw std::runtime_error(connection.database.lastError().text().toUtf8().toStdString());
    } catch (...) {
        connection.database.rollback();
        throw;
    }
    return catalog;
}

CatalogParameterBinding resolveCatalogParameterBinding(
    const std::string& sqlitePath, const std::string& blockType,
    const std::string& parameterGroup, unsigned channelIndex)
{
    Connection connection(sqlitePath);
    QSqlQuery query(connection.database);
    query.prepare(QStringLiteral(
        "SELECT block_type,slot_id,parameter_group,channel_index,source,"
        "locator_type,locator,mask,mode,stimulus_route,stimulus_offset,confirmed FROM catalog_parameter_bindings "
        "WHERE block_type=? AND parameter_group=? AND channel_index=?"));
    query.addBindValue(QString::fromUtf8(blockType));
    query.addBindValue(QString::fromUtf8(parameterGroup));
    query.addBindValue(channelIndex);
    requirePrepared(query);
    if (!query.next()) {
        throw std::runtime_error("В каталоге нет привязки " + blockType + "/"
            + parameterGroup + "/" + std::to_string(channelIndex + 1));
    }
    CatalogParameterBinding result;
    result.blockType = query.value(0).toString().toUtf8().toStdString();
    result.slotId = query.value(1).toString().toUtf8().toStdString();
    result.parameterGroup = query.value(2).toString().toUtf8().toStdString();
    result.channelIndex = query.value(3).toUInt();
    result.source = query.value(4).toString().toUtf8().toStdString();
    result.locatorType = query.value(5).toString().toUtf8().toStdString();
    result.locator = query.value(6).toString().toUtf8().toStdString();
    result.mask = query.value(7).toUInt();
    result.mode = query.value(8).toUInt();
    result.stimulusRoute = query.value(9).toString().toUtf8().toStdString();
    result.stimulusOffset = query.value(10).toUInt();
    result.confirmed = query.value(11).toBool();
    return result;
}

StandCatalog loadCatalog(const std::string& sqlitePath)
{
    Connection connection(sqlitePath);
    StandCatalog catalog;
    QSqlQuery query(connection.database);
    if (!query.exec(QStringLiteral("SELECT key,value FROM catalog_meta"))) {
        throw std::runtime_error(query.lastError().text().toUtf8().toStdString());
    }
    while (query.next()) {
        if (query.value(0).toString() == QStringLiteral("version")) catalog.version = query.value(1).toString().toUtf8().toStdString();
        if (query.value(0).toString() == QStringLiteral("title")) catalog.title = query.value(1).toString().toUtf8().toStdString();
    }
    query.exec(QStringLiteral("SELECT id,name FROM catalog_cell_types ORDER BY id"));
    while (query.next()) catalog.cellTypes.push_back({query.value(0).toString().toUtf8().toStdString(), query.value(1).toString().toUtf8().toStdString()});
    query.exec(QStringLiteral("SELECT id,name,designation FROM catalog_block_types ORDER BY id"));
    while (query.next()) catalog.blockTypes.push_back({query.value(0).toString().toUtf8().toStdString(), query.value(1).toString().toUtf8().toStdString(), query.value(2).toString().toUtf8().toStdString(), {}});
    for (auto& block : catalog.blockTypes) {
        query.prepare(QStringLiteral("SELECT slot_id,cell_type,sort_order FROM catalog_block_slots WHERE block_type=? ORDER BY sort_order"));
        query.addBindValue(QString::fromUtf8(block.id)); requirePrepared(query);
        while (query.next()) block.cellSlots.push_back({query.value(0).toString().toUtf8().toStdString(), query.value(1).toString().toUtf8().toStdString(), query.value(2).toUInt()});
        query.finish();
    }
    query.exec(QStringLiteral("SELECT id,block_type,name,serial FROM catalog_block_instances ORDER BY name"));
    while (query.next()) catalog.instances.push_back({query.value(0).toString().toUtf8().toStdString(), query.value(1).toString().toUtf8().toStdString(), query.value(2).toString().toUtf8().toStdString(), query.value(3).toString().toUtf8().toStdString()});
    return catalog;
}

} // namespace orbita::stand
