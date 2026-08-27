#include "plugin_support.h"
#include "orbita_stand/equipment_adapters.h"

#include <memory>

namespace {
using namespace orbita::stand;
struct Instance { std::map<std::string, std::string> config; std::unique_ptr<RigolVisaGenerator> generator; };
orbita_plugin_status_v1 create(const char*, const char* text, void** output, orbita_plugin_buffer_v1* diagnostic)
{
    return plugin::guarded(diagnostic, [&] {
        if (!output) throw std::invalid_argument("Instance output pointer is required");
        auto instance = std::make_unique<Instance>();
        instance->config = plugin::arguments(text);
        RigolVisaConfig config;
        if (instance->config.count("resource")) config.resourceExpressions = {instance->config["resource"]};
        config.timeoutMilliseconds = plugin::unsignedValue(instance->config, "timeout_ms", config.timeoutMilliseconds);
        instance->generator = std::make_unique<RigolVisaGenerator>(std::move(config));
        *output = instance.release();
        return std::string("Rigol generator VISA session created");
    });
}
void destroy(void* value) { delete static_cast<Instance*>(value); }
orbita_plugin_status_v1 invoke(void* value, const char* capability, const char* operation,
                              const char* request, orbita_plugin_buffer_v1* response)
{
    return plugin::guarded(response, [&] {
        auto& instance = *static_cast<Instance*>(value);
        if (!capability || std::string(capability) != "signal.generator") throw std::invalid_argument("Unsupported capability");
        const std::string command = operation ? operation : "";
        const auto args = plugin::arguments(request);
        if (command == "probe") return "idn=" + instance.generator->identity() + "resource=" + instance.generator->resourceName() + "\n";
        plugin::requireActiveOutputs(instance.config);
        if (command == "set_sine") instance.generator->setSine(
            plugin::unsignedValue(args, "channel", 1), plugin::doubleValue(args, "frequency_hz"),
            plugin::doubleValue(args, "amplitude_vpp"), plugin::doubleValue(args, "offset_v", 0.0));
        else if (command == "output") instance.generator->output(
            plugin::unsignedValue(args, "channel", 1), plugin::booleanValue(args, "enabled"));
        else throw std::invalid_argument("Unsupported generator operation: " + command);
        return std::string("status=ok\n");
    });
}
void cancel(void*) {}
void safeStop(void* value)
{
    if (!value) return;
    auto& instance = *static_cast<Instance*>(value);
    if (!plugin::booleanValue(instance.config, "profile.active_outputs_confirmed")) return;
    try { instance.generator->output(1, false); } catch (...) {}
    try { instance.generator->output(2, false); } catch (...) {}
}
const orbita_equipment_api_v1 api{ORBITA_EQUIPMENT_ABI_V1, sizeof(orbita_equipment_api_v1),
    "orbita.rigol_generator", "Генератор Rigol DG", "signal.generator",
    create, destroy, invoke, cancel, safeStop};
}
extern "C" ORBITA_PLUGIN_EXPORT const orbita_equipment_api_v1* orbita_plugin_get_api_v1(void) { return &api; }
