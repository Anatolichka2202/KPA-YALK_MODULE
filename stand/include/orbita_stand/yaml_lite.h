#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace orbita::stand::yaml {

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Node {
    enum class Type { Null, Scalar, Map, Sequence };
    Type type = Type::Null;
    std::string scalar;
    std::map<std::string, Node> map;
    std::vector<Node> sequence;

    bool isNull() const noexcept { return type == Type::Null; }
    bool isScalar() const noexcept { return type == Type::Scalar; }
    bool isMap() const noexcept { return type == Type::Map; }
    bool isSequence() const noexcept { return type == Type::Sequence; }
    const Node& at(const std::string& key) const;
    const Node* find(const std::string& key) const noexcept;
    std::string value(const std::string& key, std::string fallback = {}) const;
};

Node parse(const std::string& document);
Node parseFile(const std::string& path);

} // namespace orbita::stand::yaml
