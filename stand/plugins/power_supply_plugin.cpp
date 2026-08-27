#include "plugin_support.h"
#include "orbita_stand/equipment_adapters.h"

#include <memory>

namespace {
using namespace orbita::stand;
struct Instance { std::map<std::string, std::string> config; std::unique_ptr<LegacyUdpPowerSupply> supply; };
orbita_plugin_status_v1 create(const char*, const char* text, void** output, orbita_plugin_buffer_v1* diagnostic)
{
    return plugin::guarded(diagnostic, [&] {
        if (!output) throw std::invalid_argument("Instance output pointer is required");
        auto instance = std::make_unique<Instance>();
        instance->config = plugin::arguments(text);
        const bool active = plugin::booleanValue(instance->config, "profile.active_outputs_confirmed")
            && plugin::booleanValue(instance->config, "allow_legacy_commands");
        instance->supply = std::make_unique<LegacyUdpPowerSupply>(LegacyUdpPowerSupplyConfig{
            plugin::required(instance->config, "host"),
            static_cast<std::uint16_t>(plugin::unsignedValue(instance->config, "command_port", 4001)),
            static_cast<std::uint16_t>(plugin::unsignedValue(instance->config, "reply_port", 6008)),
            plugin::unsignedValue(instance->config, "timeout_ms", 1500), active});
        *output = instance.release();
        return std::string("Power supply UDP adapter created");
    });
}
void destroy(void* value) { delete static_cast<Instance*>(value); }
orbita_plugin_status_v1 invoke(void* value, const char* capability, const char* operation,
                              const char* request, orbita_plugin_buffer_v1* response)
{
    return plugin::guarded(response, [&] {
        auto& instance = *static_cast<Instance*>(value);
        if (!capability || std::string(capability) != "power.dc_supply") throw std::invalid_argument("Unsupported capability");
        const std::string command = operation ? operation : "";
        const auto args = plugin::arguments(request);
        if (command == "probe" || command == "read_state") return instance.supply->probe();
        plugin::requireActiveOutputs(instance.config);
        if (command == "set_voltage") instance.supply->setVoltage(plugin::doubleValue(args, "volts"));
        else if (command == "set_current_limit") instance.supply->setCurrent(plugin::doubleValue(args, "amperes"));
        else if (command == "output") {
            if (plugin::booleanValue(args, "enabled")) instance.supply->outputOn(); else instance.supply->outputOff();
        } else throw std::invalid_argument("Unsupported power supply operation: " + command);
        return std::string("status=ok\n");
    });
}
void cancel(void*) {}
void safeStop(void* value)
{
    if (!value) return;
    auto& instance = *static_cast<Instance*>(value);
    if (!plugin::booleanValue(instance.config, "profile.active_outputs_confirmed")) return;
    try { instance.supply->outputOff(); } catch (...) {}
}
const orbita_equipment_api_v1 api{ORBITA_EQUIPMENT_ABI_V1, sizeof(orbita_equipment_api_v1),
    "orbita.power_udp", "Источник питания через Ethernet-мост", "power.dc_supply",
    create, destroy, invoke, cancel, safeStop};
}
extern "C" ORBITA_PLUGIN_EXPORT const orbita_equipment_api_v1* orbita_plugin_get_api_v1(void) { return &api; }
