#include "plugin_support.h"
#include "orbita_stand/equipment_adapters.h"

#include <memory>
#include <sstream>

namespace {
using namespace orbita::stand;

std::vector<unsigned> channels(const std::string& value)
{
    std::vector<unsigned> result;
    std::stringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) if (!token.empty()) result.push_back(std::stoul(token));
    return result;
}

struct Instance {
    std::map<std::string, std::string> config;
    std::unique_ptr<IsdHttpRouter> router;
};

orbita_plugin_status_v1 create(const char*, const char* text, void** output, orbita_plugin_buffer_v1* diagnostic)
{
    return plugin::guarded(diagnostic, [&] {
        if (!output) throw std::invalid_argument("Instance output pointer is required");
        auto instance = std::make_unique<Instance>();
        instance->config = plugin::arguments(text);
        instance->router = std::make_unique<IsdHttpRouter>(IsdHttpConfig{
            plugin::required(instance->config, "host"),
            static_cast<std::uint16_t>(plugin::unsignedValue(instance->config, "port", 80)),
            plugin::unsignedValue(instance->config, "timeout_ms", 1500),
            plugin::unsignedValue(instance->config, "switch_type", 2),
            channels(instance->config["reset_channels"])});
        *output = instance.release();
        return std::string("ISD HTTP router created");
    });
}
void destroy(void* value) { delete static_cast<Instance*>(value); }
orbita_plugin_status_v1 invoke(void* value, const char* capability, const char* operation,
                              const char* request, orbita_plugin_buffer_v1* response)
{
    return plugin::guarded(response, [&] {
        auto& instance = *static_cast<Instance*>(value);
        if (!capability || std::string(capability) != "stand.switch_matrix") throw std::invalid_argument("Unsupported capability");
        std::string command = operation ? operation : "";
        if (command == "set_analog") command = "analog";
        if (command == "set_switch") command = "switch";
        const auto args = plugin::arguments(request);
        if (command == "probe") {
            const auto body = instance.router->probe();
            return std::string("status=ready\nalive=1\nmessage=ИСД ответил по HTTP\nresponse=")
                + body + "\n";
        }
        plugin::requireActiveOutputs(instance.config);
        const auto resolvedChannel = [&]() {
            if (args.count("route")) {
                const std::string key = "route." + args.at("route");
                if (!instance.config.count(key)) throw std::invalid_argument("Не назначен маршрут ИСД " + args.at("route"));
                if (args.count("ulk_address")) {
                    const unsigned address = plugin::unsignedValue(args, "ulk_address");
                    if (!address) throw std::invalid_argument("Адрес УЛК начинается с 1");
                    return plugin::unsignedValue(instance.config, key) + address - 1;
                }
                return plugin::unsignedValue(instance.config, key) + plugin::unsignedValue(args, "offset", 0);
            }
            return plugin::unsignedValue(args, "channel");
        };
        if (command == "reset" || command == "full_reset") instance.router->reset();
        else if (command == "switch") {
            unsigned type = plugin::unsignedValue(args, "type",
                plugin::unsignedValue(instance.config, "switch_type", 2));
            if (args.count("route")) type = plugin::unsignedValue(instance.config,
                "route." + args.at("route") + ".type", type);
            instance.router->setSwitch(type, resolvedChannel(), plugin::booleanValue(args, "enabled"));
        } else if (command == "analog") {
            const unsigned value = args.count("code")
                ? plugin::unsignedValue(args, "code") : plugin::unsignedValue(args, "value");
            if (args.count("route")) {
                const std::string prefix = "route." + args.at("route") + ".";
                const unsigned minimum = plugin::unsignedValue(instance.config, prefix + "min", 0);
                const unsigned maximum = plugin::unsignedValue(instance.config, prefix + "max", 4095);
                if (value < minimum || value > maximum) throw std::invalid_argument(
                    "Значение ИСД вне разрешённого диапазона маршрута " + args.at("route"));
            }
            instance.router->setAnalog(resolvedChannel(), value,
                plugin::booleanValue(args, "enabled"));
        }
        else throw std::invalid_argument("Unsupported ISD operation: " + command);
        return std::string("status=ok\noperation=") + command + "\n";
    });
}
void cancel(void*) {}
void safeStop(void* value)
{
    if (!value) return;
    auto& instance = *static_cast<Instance*>(value);
    try { if (plugin::booleanValue(instance.config, "profile.active_outputs_confirmed")
        || plugin::booleanValue(instance.config, "device.active_commands_confirmed")) instance.router->reset(); }
    catch (...) {}
}
const orbita_equipment_api_v1 api{ORBITA_EQUIPMENT_ABI_V1, sizeof(orbita_equipment_api_v1),
    "orbita.isd_http", "Имитатор сигналов датчиков", "stand.switch_matrix",
    create, destroy, invoke, cancel, safeStop};
}
extern "C" ORBITA_PLUGIN_EXPORT const orbita_equipment_api_v1* orbita_plugin_get_api_v1(void) { return &api; }
