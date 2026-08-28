#include "orbita_stand/config.h"
#include "orbita_stand/yaml_lite.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace orbita::stand {
namespace {

bool boolean(const std::string& value, bool fallback)
{
    std::string normalized;
    std::transform(value.begin(), value.end(), std::back_inserter(normalized),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (normalized == "true" || normalized == "yes" || normalized == "1") return true;
    if (normalized == "false" || normalized == "no" || normalized == "0") return false;
    return fallback;
}

std::vector<std::string> stringSequence(const yaml::Node* node)
{
    std::vector<std::string> result;
    if (!node) return result;
    if (!node->isSequence()) throw yaml::Error("Expected a YAML sequence");
    for (const auto& item : node->sequence) {
        if (!item.isScalar()) throw yaml::Error("Expected a scalar sequence item");
        result.push_back(item.scalar);
    }
    return result;
}

std::map<std::string, std::string> stringMap(const yaml::Node* node)
{
    std::map<std::string, std::string> result;
    if (!node) return result;
    if (!node->isMap()) throw yaml::Error("Expected a YAML mapping");
    for (const auto& [key, value] : node->map) {
        if (!value.isScalar()) throw yaml::Error("Expected scalar value for YAML key " + key);
        result[key] = value.scalar;
    }
    return result;
}

std::map<std::string, std::string> routeMap(const yaml::Node* node)
{
    std::map<std::string, std::string> result;
    if (!node) return result;
    if (!node->isMap()) throw yaml::Error("Expected routes to be a YAML mapping");
    for (const auto& [name, value] : node->map) {
        if (value.isScalar()) {
            result[name] = value.scalar;
            continue;
        }
        if (!value.isMap()) throw yaml::Error("Route " + name + " must be scalar or mapping");
        for (const auto& [field, fieldValue] : value.map) {
            if (!fieldValue.isScalar()) throw yaml::Error(
                "Route field " + name + "." + field + " must be scalar");
            result[name + "." + field] = fieldValue.scalar;
            if (field == "base") result[name] = fieldValue.scalar;
        }
        if (!result.count(name)) throw yaml::Error("Route " + name + " requires base");
    }
    return result;
}

ScenarioNode scenarioNode(const yaml::Node& value)
{
    if (!value.isMap()) throw yaml::Error("Scenario step must be a mapping");
    ScenarioNode node;
    node.id = value.value("id");
    node.title = value.value("title");
    node.tuRequirement = value.value("tu");
    node.procedure = value.value("procedure");
    node.requiredCapabilities = {};
    for (const auto& capability : stringSequence(value.find("requires"))) {
        node.requiredCapabilities.insert(capability);
    }
    node.arguments = stringMap(value.find("args"));
    if (const auto* children = value.find("steps")) {
        if (!children->isSequence()) throw yaml::Error("Scenario steps must be a sequence");
        for (const auto& child : children->sequence) node.children.push_back(scenarioNode(child));
    }
    return node;
}

} // namespace

StandProfile loadStandProfile(const std::string& path)
{
    const auto root = yaml::parseFile(path);
    if (!root.isMap()) throw yaml::Error("Stand profile root must be a mapping");
    if (root.value("schema") != "1") throw yaml::Error("Unsupported stand profile schema");
    StandProfile profile;
    profile.id = root.value("id");
    profile.version = root.value("version");
    profile.title = root.value("title");
    profile.activeOutputsConfirmed = boolean(root.value("active_outputs_confirmed"), false);
    profile.routes = routeMap(root.find("routes"));
    profile.connections = stringMap(root.find("connections"));
    const auto& devices = root.at("devices");
    if (!devices.isSequence()) throw yaml::Error("Profile devices must be a sequence");
    for (const auto& value : devices.sequence) {
        if (!value.isMap()) throw yaml::Error("Profile device must be a mapping");
        DeviceProfile device;
        device.id = value.value("id");
        device.pluginId = value.value("plugin");
        device.enabled = boolean(value.value("enabled", "true"), true);
        device.bindCapabilities = stringSequence(value.find("bind"));
        device.configuration = stringMap(value.find("config"));
        if (device.id.empty() || device.pluginId.empty()) {
            throw yaml::Error("Every profile device requires id and plugin");
        }
        profile.devices.push_back(std::move(device));
    }
    if (profile.id.empty() || profile.version.empty()) {
        throw yaml::Error("Stand profile requires id and version");
    }
    return profile;
}

ScenarioDefinition loadScenarioYaml(const std::string& path)
{
    const auto root = yaml::parseFile(path);
    if (!root.isMap()) throw yaml::Error("Scenario root must be a mapping");
    if (root.value("schema") != "1") throw yaml::Error("Unsupported scenario schema");
    ScenarioDefinition scenario;
    scenario.id = root.value("id");
    scenario.title = root.value("title");
    scenario.version = root.value("version");
    scenario.catalogVersion = root.value("catalog_version");
    scenario.objectType = root.value("object_type");
    scenario.publicationState = root.value("state") == "published"
        ? PublicationState::Published : PublicationState::Draft;
    const auto& steps = root.at("steps");
    if (!steps.isSequence()) throw yaml::Error("Scenario steps must be a sequence");
    for (const auto& step : steps.sequence) scenario.steps.push_back(scenarioNode(step));
    return scenario;
}

void instantiateProfile(
    const StandProfile& profile,
    EquipmentPluginManager& manager,
    EquipmentRegistry& registry,
    std::vector<std::shared_ptr<EquipmentDevice>>& devices)
{
    registry.clear();
    devices.clear();
    for (const auto& definition : profile.devices) {
        if (!definition.enabled) continue;
        auto config = definition.configuration;
        config["profile.active_outputs_confirmed"] = profile.activeOutputsConfirmed ? "true" : "false";
        for (const auto& [key, value] : profile.routes) config["route." + key] = value;
        auto device = manager.createDevice(definition.pluginId, definition.id, config);
        for (const auto& capability : definition.bindCapabilities) registry.bind(capability, device);
        devices.push_back(std::move(device));
    }
}

} // namespace orbita::stand
