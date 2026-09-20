#include "backend/scenario_yaml.h"

#include <yaml-cpp/yaml.h>

#include <sstream>
#include <stdexcept>

namespace tu {
namespace {

std::string scalarOrSequence(const YAML::Node& node)
{
    if (!node) return {};
    if (node.IsScalar()) return node.as<std::string>();
    if (node.IsSequence()) {
        std::ostringstream out;
        bool first = true;
        for (const auto& item : node) {
            if (!item.IsScalar())
                throw std::runtime_error("Аргумент сценария содержит вложенную структуру");
            if (!first) out << ',';
            first = false;
            out << item.as<std::string>();
        }
        return out.str();
    }
    throw std::runtime_error("Аргумент сценария должен быть scalar или sequence");
}

std::string requiredString(const YAML::Node& node, const char* key)
{
    const auto value = node[key];
    if (!value || !value.IsScalar())
        throw std::runtime_error(std::string("В сценарии отсутствует поле ") + key);
    return value.as<std::string>();
}

} // namespace

ScenarioDefinition loadScenarioYaml(const std::filesystem::path& path)
{
    const YAML::Node root = YAML::LoadFile(path.string());
    if (!root || !root.IsMap()) throw std::runtime_error("Некорректный YAML сценария");

    ScenarioDefinition scenario;
    scenario.id = requiredString(root, "id");
    scenario.title = requiredString(root, "title");
    scenario.version = requiredString(root, "version");

    const auto steps = root["steps"];
    if (!steps || !steps.IsSequence())
        throw std::runtime_error("В YAML сценария отсутствует список steps");

    for (const auto& source : steps) {
        if (!source.IsMap()) throw std::runtime_error("Шаг сценария должен быть map");
        ScenarioStep step;
        step.id = requiredString(source, "id");
        step.title = requiredString(source, "title");
        step.tuRequirement = requiredString(source, "tu");
        step.procedure = requiredString(source, "procedure");

        if (const auto args = source["args"]) {
            if (!args.IsMap()) throw std::runtime_error("args шага должен быть map");
            for (const auto& item : args) {
                step.arguments.emplace(
                    item.first.as<std::string>(), scalarOrSequence(item.second));
            }
        }
        scenario.steps.push_back(std::move(step));
    }

    return scenario;
}

} // namespace tu
