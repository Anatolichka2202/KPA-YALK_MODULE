#include "orbita_stand/yaml_lite.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

namespace orbita::stand::yaml {
namespace {

struct Line {
    std::size_t number = 0;
    std::size_t indent = 0;
    std::string text;
};

std::string trim(const std::string& value)
{
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

std::string stripComment(const std::string& value)
{
    bool single = false;
    bool quoted = false;
    bool escape = false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (escape) { escape = false; continue; }
        if (c == '\\' && quoted) { escape = true; continue; }
        if (c == '\'' && !quoted) single = !single;
        else if (c == '"' && !single) quoted = !quoted;
        else if (c == '#' && !single && !quoted && (i == 0 || std::isspace(static_cast<unsigned char>(value[i - 1])))) {
            return value.substr(0, i);
        }
    }
    return value;
}

std::size_t separator(const std::string& value)
{
    bool single = false;
    bool quoted = false;
    bool escape = false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (escape) { escape = false; continue; }
        if (c == '\\' && quoted) { escape = true; continue; }
        if (c == '\'' && !quoted) single = !single;
        else if (c == '"' && !single) quoted = !quoted;
        else if (c == ':' && !single && !quoted) return i;
    }
    return std::string::npos;
}

std::string scalarValue(std::string value)
{
    value = trim(value);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"')
        || (value.front() == '\'' && value.back() == '\''))) {
        const char quote = value.front();
        value = value.substr(1, value.size() - 2);
        if (quote == '"') {
            std::string decoded;
            bool escape = false;
            for (const char c : value) {
                if (escape) {
                    switch (c) {
                    case 'n': decoded.push_back('\n'); break;
                    case 'r': decoded.push_back('\r'); break;
                    case 't': decoded.push_back('\t'); break;
                    default: decoded.push_back(c); break;
                    }
                    escape = false;
                } else if (c == '\\') escape = true;
                else decoded.push_back(c);
            }
            if (escape) decoded.push_back('\\');
            return decoded;
        }
    }
    return value;
}

Node makeScalar(const std::string& value)
{
    Node node;
    node.type = Node::Type::Scalar;
    node.scalar = scalarValue(value);
    return node;
}

std::vector<Line> lines(const std::string& document)
{
    std::vector<Line> result;
    std::istringstream stream(document);
    std::string raw;
    std::size_t number = 0;
    while (std::getline(stream, raw)) {
        ++number;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        raw = stripComment(raw);
        if (trim(raw).empty() || trim(raw) == "---" || trim(raw) == "...") continue;
        std::size_t indent = 0;
        while (indent < raw.size() && raw[indent] == ' ') ++indent;
        if (indent < raw.size() && raw[indent] == '\t') {
            throw Error("YAML line " + std::to_string(number) + ": tab indentation is forbidden");
        }
        result.push_back({number, indent, trim(raw.substr(indent))});
    }
    return result;
}

Node parseBlock(const std::vector<Line>& input, std::size_t& index, std::size_t indent);

void parseMapEntry(
    Node& target,
    const std::vector<Line>& input,
    std::size_t& index,
    std::size_t indent,
    const std::string& entry)
{
    const auto colon = separator(entry);
    if (colon == std::string::npos) {
        throw Error("YAML line " + std::to_string(input[index].number) + ": expected key: value");
    }
    const auto key = trim(entry.substr(0, colon));
    const auto value = trim(entry.substr(colon + 1));
    if (key.empty()) throw Error("YAML line " + std::to_string(input[index].number) + ": empty key");
    if (target.map.count(key)) throw Error("YAML line " + std::to_string(input[index].number) + ": duplicate key " + key);
    ++index;
    if (!value.empty()) target.map.emplace(key, makeScalar(value));
    else if (index < input.size() && input[index].indent > indent) {
        target.map.emplace(key, parseBlock(input, index, input[index].indent));
    } else target.map.emplace(key, Node{});
}

Node parseMap(const std::vector<Line>& input, std::size_t& index, std::size_t indent)
{
    Node node;
    node.type = Node::Type::Map;
    while (index < input.size() && input[index].indent == indent
           && input[index].text.rfind("-", 0) != 0) {
        parseMapEntry(node, input, index, indent, input[index].text);
    }
    return node;
}

Node parseSequence(const std::vector<Line>& input, std::size_t& index, std::size_t indent)
{
    Node node;
    node.type = Node::Type::Sequence;
    while (index < input.size() && input[index].indent == indent
           && input[index].text.rfind("-", 0) == 0) {
        const auto itemText = trim(input[index].text.substr(1));
        if (itemText.empty()) {
            ++index;
            if (index >= input.size() || input[index].indent <= indent) node.sequence.push_back(Node{});
            else node.sequence.push_back(parseBlock(input, index, input[index].indent));
            continue;
        }
        if (separator(itemText) == std::string::npos) {
            node.sequence.push_back(makeScalar(itemText));
            ++index;
            continue;
        }
        Node item;
        item.type = Node::Type::Map;
        parseMapEntry(item, input, index, indent, itemText);
        if (index < input.size() && input[index].indent > indent) {
            const std::size_t childIndent = input[index].indent;
            while (index < input.size() && input[index].indent == childIndent
                   && input[index].text.rfind("-", 0) != 0) {
                parseMapEntry(item, input, index, childIndent, input[index].text);
            }
        }
        node.sequence.push_back(std::move(item));
    }
    return node;
}

Node parseBlock(const std::vector<Line>& input, std::size_t& index, std::size_t indent)
{
    if (index >= input.size()) return {};
    if (input[index].indent != indent) {
        throw Error("YAML line " + std::to_string(input[index].number) + ": inconsistent indentation");
    }
    return input[index].text.rfind("-", 0) == 0
        ? parseSequence(input, index, indent)
        : parseMap(input, index, indent);
}

} // namespace

const Node& Node::at(const std::string& key) const
{
    if (!isMap()) throw Error("YAML node is not a mapping while reading " + key);
    const auto iterator = map.find(key);
    if (iterator == map.end()) throw Error("Missing YAML key: " + key);
    return iterator->second;
}

const Node* Node::find(const std::string& key) const noexcept
{
    if (!isMap()) return nullptr;
    const auto iterator = map.find(key);
    return iterator == map.end() ? nullptr : &iterator->second;
}

std::string Node::value(const std::string& key, std::string fallback) const
{
    const auto* node = find(key);
    return node && node->isScalar() ? node->scalar : std::move(fallback);
}

Node parse(const std::string& document)
{
    const auto input = lines(document);
    if (input.empty()) return {};
    std::size_t index = 0;
    auto root = parseBlock(input, index, input.front().indent);
    if (index != input.size()) {
        throw Error("YAML line " + std::to_string(input[index].number) + ": unexpected indentation");
    }
    return root;
}

Node parseFile(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw Error("Cannot open YAML file: " + path);
    std::ostringstream document;
    document << stream.rdbuf();
    return parse(document.str());
}

} // namespace orbita::stand::yaml
