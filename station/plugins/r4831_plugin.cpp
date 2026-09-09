#include "plugin_support.h"
#include "orbita_stand/equipment_adapters.h"

#include <memory>

namespace {
using namespace orbita::stand;
struct Instance { std::map<std::string, std::string> config; std::unique_ptr<R4831SerialAdapter> resistance; };
orbita_plugin_status_v1 create(const char*, const char* text, void** output, orbita_plugin_buffer_v1* diagnostic)
{
    return plugin::guarded(diagnostic, [&] {
        if (!output) throw std::invalid_argument("Instance output pointer is required");
        auto instance = std::make_unique<Instance>();
        instance->config = plugin::arguments(text);
        instance->resistance = std::make_unique<R4831SerialAdapter>(R4831SerialConfig{
            plugin::required(instance->config, "port"),
            static_cast<int>(plugin::unsignedValue(instance->config, "baud", 9600)),
            plugin::unsignedValue(instance->config, "timeout_ms", 1000),
            plugin::booleanValue(instance->config, "decimal_comma")});
        *output = instance.release();
        return std::string("R4831 serial adapter created");
    });
}
void destroy(void* value) { delete static_cast<Instance*>(value); }
orbita_plugin_status_v1 invoke(void* value, const char* capability, const char* operation,
                              const char* request, orbita_plugin_buffer_v1* response)
{
    return plugin::guarded(response, [&] {
        auto& instance = *static_cast<Instance*>(value);
        if (!capability || std::string(capability) != "signal.resistance") throw std::invalid_argument("Unsupported capability");
        const std::string command = operation ? operation : "";
        if (command == "probe") return instance.resistance->probe();
        plugin::requireActiveOutputs(instance.config);
        if (command == "set_resistance") instance.resistance->setResistance(
            plugin::doubleValue(plugin::arguments(request), "ohms"));
        else throw std::invalid_argument("Unsupported R4831 operation: " + command);
        return std::string("status=ok\n");
    });
}
void cancel(void*) {}
void safeStop(void*) {}
const orbita_equipment_api_v1 api{ORBITA_EQUIPMENT_ABI_V1, sizeof(orbita_equipment_api_v1),
    "orbita.r4831", "Магазин сопротивлений Р4831", "signal.resistance",
    create, destroy, invoke, cancel, safeStop};
}
extern "C" ORBITA_PLUGIN_EXPORT const orbita_equipment_api_v1* orbita_plugin_get_api_v1(void) { return &api; }
