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

unsigned consumeTechnicalRetries(std::map<std::string, std::string>& arguments)
{
    const auto found = arguments.find("technical_retries");
    if (found == arguments.end()) return 0;

    const std::string text = found->second;
    arguments.erase(found);
    try {
        std::size_t parsed = 0;
        const auto retries = std::stoull(text, &parsed);
        if (parsed != text.size() || retries > 3) {
            throw std::invalid_argument("out of range");
        }
        return static_cast<unsigned>(retries);
    } catch (...) {
        throw yaml::Error("Scenario technical_retries must be an integer from 0 to 3");
    }
}

std::vector<ResourceRequirement> resourceRequirements(const yaml::Node* node)
{
    std::vector<ResourceRequirement> result;
    if (!node) return result;
    if (!node->isSequence()) throw yaml::Error("Scenario resources must be a YAML sequence");
    for (const auto& item : node->sequence) {
        if (!item.isMap()) throw yaml::Error("Scenario resource requirement must be a mapping");
        ResourceRequirement requirement;
        requirement.resource = item.value("resource");
        requirement.capability = item.value("capability");
        if (requirement.resource.empty() || requirement.capability.empty()) {
            throw yaml::Error("Scenario resource requirement needs resource and capability");
        }
        result.push_back(std::move(requirement));
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

ComponentProfile componentProfile(const yaml::Node& value)
{
    if (!value.isMap()) throw yaml::Error("Profile component must be a mapping");
    ComponentProfile component;
    component.id = value.value("id");
    component.kind = value.value("kind");
    component.provider = value.value("provider");
    component.enabled = boolean(value.value("enabled", "true"), true);
    component.bindings = stringSequence(value.find("bind"));
    component.configuration = stringMap(value.find("config"));
    component.capabilities = stringSequence(value.find("capabilities"));
    if (component.id.empty() || component.kind.empty() || component.provider.empty()) {
        throw yaml::Error("Every profile component requires id, kind and provider");
    }
    if (component.kind != "equipment" && !component.capabilities.empty()) {
        throw yaml::Error("Only equipment components may declare capabilities: " + component.id);
    }
    return component;
}

DeviceProfile deviceProfile(const yaml::Node& value)
{
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
    return device;
}

ComponentProfile asComponent(const DeviceProfile& device)
{
    ComponentProfile component;
    component.id = device.id;
    component.kind = "equipment";
    component.provider = device.pluginId;
    component.enabled = device.enabled;
    // Legacy `devices:` never had a separate role dimension. Keep bind as the
    // historical capability list and leave capabilities empty so callers can
    // distinguish this compatibility representation from the canonical form.
    component.bindings = device.bindCapabilities;
    component.configuration = device.configuration;
    return component;
}

DeviceProfile asDevice(const ComponentProfile& component)
{
    if (component.kind != "equipment") {
        throw std::invalid_argument("Only equipment components can be projected as DeviceProfile");
    }
    DeviceProfile device;
    device.id = component.id;
    device.pluginId = component.provider;
    device.enabled = component.enabled;
    device.bindCapabilities = component.capabilities.empty()
        ? component.bindings
        : component.capabilities;
    device.configuration = component.configuration;
    return device;
}

bool hasComponentId(const std::vector<ComponentProfile>& components, const std::string& id)
{
    return std::any_of(components.begin(), components.end(),
        [&](const ComponentProfile& component) { return component.id == id; });
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
    node.policy.technicalRetries = consumeTechnicalRetries(node.arguments);
    node.requiredResources = resourceRequirements(value.find("resources"));
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

    if (const auto* components = root.find("components")) {
        if (!components->isSequence()) throw yaml::Error("Profile components must be a sequence");
        for (const auto& value : components->sequence) {
            auto component = componentProfile(value);
            if (hasComponentId(profile.components, component.id)) {
                throw yaml::Error("Duplicate profile component id: " + component.id);
            }
            if (component.kind == "equipment") {
                // Existing desktop/equipment code still consumes DeviceProfile.
                // The view is generated from the canonical component declaration;
                // the delivery no longer needs to duplicate equipment in `devices:`.
                profile.devices.push_back(asDevice(component));
            }
            profile.components.push_back(std::move(component));
        }
    }

    // Backward-compatible input only. Legacy deliveries may still contain
    // `devices:`, but one instance must be declared in exactly one section.
    // Each legacy device is projected into the canonical component model.
    if (const auto* devices = root.find("devices")) {
        if (!devices->isSequence()) throw yaml::Error("Profile devices must be a sequence");
        for (const auto& value : devices->sequence) {
            auto device = deviceProfile(value);
            if (hasComponentId(profile.components, device.id)) {
                throw yaml::Error(
                    "Component/device is declared twice; use only components: for " + device.id);
            }
            profile.components.push_back(asComponent(device));
            profile.devices.push_back(std::move(device));
        }
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

const ComponentProfile* findComponentById(
    const StandProfile& profile, const std::string& id) noexcept
{
    const auto iterator = std::find_if(profile.components.begin(), profile.components.end(),
        [&](const ComponentProfile& component) { return component.id == id; });
    return iterator == profile.components.end() ? nullptr : &*iterator;
}

const ComponentProfile* findComponentByBinding(
    const StandProfile& profile, const std::string& binding) noexcept
{
    const auto iterator = std::find_if(profile.components.begin(), profile.components.end(),
        [&](const ComponentProfile& component) {
            return component.enabled
                && std::find(component.bindings.begin(), component.bindings.end(), binding)
                    != component.bindings.end();
        });
    return iterator == profile.components.end() ? nullptr : &*iterator;
}

void instantiateProfile(
    const StandProfile& profile,
    EquipmentPluginManager& manager,
    EquipmentRegistry& registry,
    std::vector<std::shared_ptr<EquipmentDevice>>& devices)
{
    registry.clear();
    devices.clear();

    // loadStandProfile() already projects canonical kind=equipment components
    // into profile.devices for compatibility. Keep the merge below for callers
    // that construct StandProfile directly in C++ instead of loading YAML.
    std::vector<DeviceProfile> equipmentDefinitions = profile.devices;
    for (const auto& component : profile.components) {
        if (component.kind != "equipment") continue;
        const bool alreadyPresent = std::any_of(equipmentDefinitions.begin(), equipmentDefinitions.end(),
            [&](const DeviceProfile& device) { return device.id == component.id; });
        if (alreadyPresent) continue;
        equipmentDefinitions.push_back(asDevice(component));
    }

    for (const auto& definition : equipmentDefinitions) {
        if (!definition.enabled) continue;
        auto config = definition.configuration;
        config["profile.active_outputs_confirmed"] = profile.activeOutputsConfirmed ? "true" : "false";
        for (const auto& [key, value] : profile.routes) config["route." + key] = value;
        auto device = manager.createDevice(definition.pluginId, definition.id, config);

        // Every component can be addressed by its concrete instance id. A
        // canonical equipment component may additionally expose stable delivery
        // roles through `bind`; those roles are separate from capabilities.
        registry.bindResource(definition.id, device);
        if (const auto* component = findComponentById(profile, definition.id);
            component && !component->capabilities.empty()) {
            for (const auto& role : component->bindings) {
                if (role.empty() || role == definition.id) continue;
                registry.bindResource(role, device);
            }
        }

        // Capability-only routing remains during migration. DeviceProfile was
        // projected from explicit `capabilities:` for canonical declarations or
        // from legacy `bind:` when loading an old profile.
        for (const auto& capability : definition.bindCapabilities) {
            registry.bind(capability, device);
        }
        devices.push_back(std::move(device));
    }
}

} // namespace orbita::stand
