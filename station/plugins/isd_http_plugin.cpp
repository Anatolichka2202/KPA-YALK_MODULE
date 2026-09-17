#include "plugin_support.h"
#include "orbita_stand/equipment_adapters.h"
#include "orbita_stand/isd_driver.h"

#include <memory>
#include <sstream>

namespace {
using namespace orbita::stand;

std::vector<unsigned> channels(const std::string& value)
{
    std::vector<unsigned> result;
    std::stringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (!token.empty()) result.push_back(std::stoul(token));
    }
    return result;
}

std::string ownerOf(const std::map<std::string, std::string>& args,
                    const std::string& fallback = "legacy")
{
    const auto found = args.find("owner");
    return found == args.end() || found->second.empty() ? fallback : found->second;
}

struct Instance {
    std::map<std::string, std::string> config;
    std::unique_ptr<IsdHttpRouter> router;
    std::unique_ptr<IsdDriver> driver;
};

orbita_plugin_status_v1 create(const char*, const char* text, void** output,
                               orbita_plugin_buffer_v1* diagnostic)
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

        auto* router = instance->router.get();
        IsdDriverOps operations;
        operations.probe = [router] { return router->probe(); };
        operations.reset = [router] { router->reset(); };
        operations.prepareYalk = [router] { router->prepareYalk(); };
        operations.setSwitch = [router](unsigned type, unsigned channel, bool enabled) {
            router->setSwitch(type, channel, enabled);
        };
        operations.setAnalog = [router](unsigned channel, unsigned value, bool enabled) {
            router->setAnalog(channel, value, enabled);
        };
        operations.setYalkVoltage = [router](unsigned channel, double volts) {
            router->setYalkVoltage(channel, volts);
        };
        operations.disableYalkOutput = [router](unsigned channel) {
            router->disableYalkOutput(channel);
        };
        instance->driver = std::make_unique<IsdDriver>(
            std::move(operations), plugin::unsignedValue(instance->config, "trace_capacity", 256));

        *output = instance.release();
        return std::string("ISD HTTP stateful driver created");
    });
}

void destroy(void* value)
{
    delete static_cast<Instance*>(value);
}

orbita_plugin_status_v1 invoke(void* value, const char* capability, const char* operation,
                              const char* request, orbita_plugin_buffer_v1* response)
{
    return plugin::guarded(response, [&] {
        auto& instance = *static_cast<Instance*>(value);
        if (!capability || std::string(capability) != "stand.switch_matrix") {
            throw std::invalid_argument("Unsupported capability");
        }

        std::string command = operation ? operation : "";
        if (command == "set_analog") command = "analog";
        if (command == "set_switch") command = "switch";
        const auto args = plugin::arguments(request);
        const auto owner = ownerOf(args);

        if (command == "probe") {
            const auto body = instance.driver->probe();
            return std::string("status=ready\nalive=1\ndriver_state=")
                + toString(instance.driver->state())
                + "\nmessage=ИСД ответил по HTTP\nresponse=" + body + "\n";
        }
        if (command == "state" || command == "driver_state") {
            return instance.driver->statusText();
        }
        if (command == "trace") {
            return instance.driver->traceText();
        }
        if (command == "clear_trace") {
            instance.driver->clearTrace();
            return std::string("status=ok\noperation=clear_trace\n");
        }
        if (command == "release" || command == "release_owner") {
            instance.driver->releaseOwner(owner);
            return std::string("status=ok\noperation=release_owner\nowner=") + owner
                + "\ndriver_state=" + toString(instance.driver->state()) + "\n";
        }

        plugin::requireActiveOutputs(instance.config);
        const auto resolvedChannel = [&]() {
            if (args.count("route")) {
                const std::string key = "route." + args.at("route");
                if (!instance.config.count(key)) {
                    throw std::invalid_argument("Не назначен маршрут ИСД " + args.at("route"));
                }
                if (args.count("ulk_address")) {
                    const unsigned address = plugin::unsignedValue(args, "ulk_address");
                    if (!address) throw std::invalid_argument("Адрес ЯЛК начинается с 1");
                    return plugin::unsignedValue(instance.config, key) + address - 1;
                }
                return plugin::unsignedValue(instance.config, key)
                    + plugin::unsignedValue(args, "offset", 0);
            }
            return plugin::unsignedValue(args, "channel");
        };

        if (command == "reset" || command == "full_reset") {
            instance.driver->reset(owner);
        } else if (command == "yalk_prepare") {
            instance.driver->prepareYalk(owner);
        } else if (command == "yalk_set_voltage") {
            instance.driver->setYalkVoltage(
                resolvedChannel(), plugin::doubleValue(args, "volts"), owner);
        } else if (command == "yalk_output_off") {
            instance.driver->disableYalkOutput(resolvedChannel(), owner);
        } else if (command == "switch") {
            unsigned type = plugin::unsignedValue(args, "type",
                plugin::unsignedValue(instance.config, "switch_type", 2));
            if (args.count("route")) {
                type = plugin::unsignedValue(instance.config,
                    "route." + args.at("route") + ".type", type);
            }
            instance.driver->setSwitch(
                type, resolvedChannel(), plugin::booleanValue(args, "enabled"), owner);
        } else if (command == "analog") {
            const unsigned value = args.count("code")
                ? plugin::unsignedValue(args, "code")
                : plugin::unsignedValue(args, "value");
            if (args.count("route")) {
                const std::string prefix = "route." + args.at("route") + ".";
                const unsigned minimum = plugin::unsignedValue(instance.config, prefix + "min", 0);
                const unsigned maximum = plugin::unsignedValue(instance.config, prefix + "max", 4095);
                if (value < minimum || value > maximum) {
                    throw std::invalid_argument(
                        "Значение ИСД вне разрешённого диапазона маршрута " + args.at("route"));
                }
            }
            instance.driver->setAnalog(
                resolvedChannel(), value, plugin::booleanValue(args, "enabled"), owner);
        } else {
            throw std::invalid_argument("Unsupported ISD operation: " + command);
        }

        return std::string("status=ok\noperation=") + command
            + "\nowner=" + owner
            + "\ndriver_state=" + toString(instance.driver->state()) + "\n";
    });
}

void cancel(void*) {}

void safeStop(void* value)
{
    if (!value) return;
    auto& instance = *static_cast<Instance*>(value);
    // Never use full type=4 as generic cleanup. The live ISD can block its
    // single HTTP worker inside the reset when an internal module does not reply.
    // Release only outputs that this plugin instance successfully enabled.
    instance.driver->safeStopAll();
}

const orbita_equipment_api_v1 api{
    ORBITA_EQUIPMENT_ABI_V1,
    sizeof(orbita_equipment_api_v1),
    "orbita.isd_http",
    "Имитатор сигналов датчиков",
    "stand.switch_matrix",
    create,
    destroy,
    invoke,
    cancel,
    safeStop};
}

extern "C" ORBITA_PLUGIN_EXPORT const orbita_equipment_api_v1* orbita_plugin_get_api_v1(void)
{
    return &api;
}
