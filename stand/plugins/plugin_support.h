#pragma once

#include "orbita_stand/equipment_plugin.h"
#include "orbita_stand/equipment_runtime.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>

namespace orbita::stand::plugin {

inline orbita_plugin_status_v1 write(
    orbita_plugin_buffer_v1* output,
    const std::string& value,
    orbita_plugin_status_v1 status = ORBITA_PLUGIN_OK)
{
    if (!output) return value.empty() ? status : ORBITA_PLUGIN_INVALID_ARGUMENT;
    output->size = value.size();
    if (value.size() > output->capacity || (value.size() && !output->data)) {
        return ORBITA_PLUGIN_BUFFER_TOO_SMALL;
    }
    if (!value.empty()) std::memcpy(output->data, value.data(), value.size());
    return status;
}

inline std::map<std::string, std::string> arguments(const char* value)
{
    return decodePluginArguments(value ? value : "");
}

inline std::string required(
    const std::map<std::string, std::string>& values,
    const std::string& key)
{
    const auto item = values.find(key);
    if (item == values.end() || item->second.empty()) throw std::invalid_argument("Missing argument: " + key);
    return item->second;
}

inline unsigned unsignedValue(
    const std::map<std::string, std::string>& values,
    const std::string& key,
    unsigned fallback = 0)
{
    const auto item = values.find(key);
    if (item == values.end() || item->second.empty()) return fallback;
    std::size_t parsed = 0;
    const auto value = std::stoul(item->second, &parsed, 0);
    if (parsed != item->second.size()) throw std::invalid_argument("Invalid unsigned argument: " + key);
    return static_cast<unsigned>(value);
}

inline double doubleValue(
    const std::map<std::string, std::string>& values,
    const std::string& key,
    double fallback = 0.0)
{
    const auto item = values.find(key);
    if (item == values.end() || item->second.empty()) return fallback;
    std::size_t parsed = 0;
    const auto value = std::stod(item->second, &parsed);
    if (parsed != item->second.size()) throw std::invalid_argument("Invalid numeric argument: " + key);
    return value;
}

inline bool booleanValue(
    const std::map<std::string, std::string>& values,
    const std::string& key,
    bool fallback = false)
{
    const auto item = values.find(key);
    if (item == values.end()) return fallback;
    return item->second == "true" || item->second == "1" || item->second == "yes" || item->second == "on";
}

inline void requireActiveOutputs(const std::map<std::string, std::string>& configuration)
{
    if (!booleanValue(configuration, "profile.active_outputs_confirmed")) {
        throw std::runtime_error(
            "Активные воздействия запрещены: подтвердите схему и установите active_outputs_confirmed: true");
    }
}

template<typename Function>
orbita_plugin_status_v1 guarded(orbita_plugin_buffer_v1* output, Function&& function)
{
    try {
        return write(output, function());
    } catch (const std::invalid_argument& error) {
        return write(output, error.what(), ORBITA_PLUGIN_INVALID_ARGUMENT);
    } catch (const std::exception& error) {
        return write(output, error.what(), ORBITA_PLUGIN_IO_ERROR);
    } catch (...) {
        return write(output, "Unknown equipment plugin error", ORBITA_PLUGIN_INTERNAL_ERROR);
    }
}

} // namespace orbita::stand::plugin
