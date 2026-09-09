#include "orbita_stand/yaml_lite.h"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <sstream>
#include <utility>

namespace orbita::stand::yaml {
namespace {

std::string location(const YAML::Mark& mark)
{
    if (mark.is_null()) {
        return {};
    }

    return " at line " + std::to_string(mark.line + 1)
        + ", column " + std::to_string(mark.column + 1);
}

Node convert(const YAML::Node& source)
{
    Node result;

    if (!source || source.IsNull()) {
        result.type = Node::Type::Null;
        return result;
    }

    if (source.IsScalar()) {
        result.type = Node::Type::Scalar;
        result.scalar = source.Scalar();
        return result;
    }

    if (source.IsSequence()) {
        result.type = Node::Type::Sequence;
        result.sequence.reserve(source.size());

        for (const auto& item : source) {
            result.sequence.push_back(convert(item));
        }

        return result;
    }

    if (source.IsMap()) {
        result.type = Node::Type::Map;

        for (const auto& entry : source) {
            if (!entry.first.IsScalar()) {
                throw Error(
                    "YAML mapping key must be a scalar"
                    + location(entry.first.Mark()));
            }

            const std::string key = entry.first.Scalar();

            if (key.empty()) {
                throw Error(
                    "YAML mapping key must not be empty"
                    + location(entry.first.Mark()));
            }

            const auto [it, inserted] =
                result.map.emplace(key, convert(entry.second));

            if (!inserted) {
                throw Error(
                    "Duplicate YAML key: " + key
                    + location(entry.first.Mark()));
            }
        }

        return result;
    }

    throw Error(
        "Unsupported YAML node type"
        + location(source.Mark()));
}

Node load(const std::string& document)
{
    try {
        return convert(YAML::Load(document));
    } catch (const Error&) {
        throw;
    } catch (const YAML::Exception& error) {
        throw Error(
            std::string("Invalid YAML")
            + location(error.mark)
            + ": "
            + error.msg);
    } catch (const std::exception& error) {
        throw Error(
            std::string("YAML parsing failed: ")
            + error.what());
    }
}

} // namespace

const Node& Node::at(const std::string& key) const
{
    if (!isMap()) {
        throw Error(
            "YAML node is not a mapping while reading " + key);
    }

    const auto iterator = map.find(key);

    if (iterator == map.end()) {
        throw Error("Missing YAML key: " + key);
    }

    return iterator->second;
}

const Node* Node::find(const std::string& key) const noexcept
{
    if (!isMap()) {
        return nullptr;
    }

    const auto iterator = map.find(key);
    return iterator == map.end()
        ? nullptr
        : &iterator->second;
}

std::string Node::value(
    const std::string& key,
    std::string fallback) const
{
    const auto* node = find(key);

    return node && node->isScalar()
        ? node->scalar
        : std::move(fallback);
}

Node parse(const std::string& document)
{
    return load(document);
}

Node parseFile(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);

    if (!stream) {
        throw Error("Cannot open YAML file: " + path);
    }

    std::ostringstream document;
    document << stream.rdbuf();

    if (!stream.good() && !stream.eof()) {
        throw Error("Cannot read YAML file: " + path);
    }

    try {
        return load(document.str());
    } catch (const Error& error) {
        throw Error(path + ": " + error.what());
    }
}

} // namespace orbita::stand::yaml