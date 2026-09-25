#include "plugin_support.h"
#include "orbita_stand/isd_driver.h"
#include "orbita_stand/isd_http_transport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <locale>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

namespace {
using namespace orbita::stand;

std::string ownerOf(const std::map<std::string, std::string>& args)
{
    const auto found = args.find("owner");
    if (found != args.end() && !found->second.empty()) return found->second;
    // Compatibility only. Production procedures should pass an explicit owner.
    return "unscoped";
}

std::vector<unsigned> addressList(const std::map<std::string, std::string>& args,
                                  const char* key)
{
    const auto found = args.find(key);
    if (found == args.end() || found->second.empty())
        throw std::invalid_argument(std::string("Missing ISD address list: ") + key);
    std::vector<unsigned> result;
    std::istringstream input(found->second);
    std::string part;
    const auto parse = [](const std::string& token) {
        if (token.empty()) throw std::invalid_argument("Empty ISD address");
        std::size_t consumed = 0;
        const auto value = std::stoul(token, &consumed);
        if (consumed != token.size() || value == 0 || value > 4096)
            throw std::invalid_argument("Invalid ISD address");
        return static_cast<unsigned>(value);
    };
    while (std::getline(input, part, ',')) {
        const auto dash = part.find('-');
        const unsigned first = parse(part.substr(0, dash));
        const unsigned last = dash == std::string::npos
            ? first : parse(part.substr(dash + 1));
        if (last < first)
            throw std::invalid_argument("Invalid ISD address range");
        for (unsigned channel = first; channel <= last; ++channel)
            result.push_back(channel);
    }
    if (result.empty()) throw std::invalid_argument("Empty ISD address list");
    return result;
}

std::string switchPath(unsigned type, unsigned channel, bool enabled)
{
    if (!type || !channel) throw std::invalid_argument("ISD type and channel start at one");
    return "/type=" + std::to_string(type) + "num=" + std::to_string(channel)
        + "val=" + (enabled ? "1" : "0");
}

std::string analogPath(unsigned channel, unsigned value, bool enabled)
{
    if (!channel) throw std::invalid_argument("ISD channel starts at one");
    return "/type=1num=" + std::to_string(channel) + "val=" + std::to_string(value)
        + "work=" + (enabled ? "1" : "0");
}

std::string yalkVoltagePath(unsigned channel, double volts)
{
    if (!channel) throw std::invalid_argument("ISD channel starts at one");
    if (!std::isfinite(volts) || volts < 0.0 || volts > 6.2) {
        throw std::invalid_argument("YALK voltage must be in range 0.00..6.20 V");
    }
    std::ostringstream value;
    value.imbue(std::locale::classic());
    value << std::fixed << std::setprecision(2) << volts;
    return "/type=5num=" + std::to_string(channel) + "val=" + value.str()
        + "work=1bus=1";
}

std::string yalkOutputBusOffPath(unsigned channel)
{
    if (!channel) throw std::invalid_argument("ISD channel starts at one");
    return "/type=1num=" + std::to_string(channel) + "val=0work=1bus=0";
}

std::string yalkOutputOffPath(unsigned channel)
{
    if (!channel) throw std::invalid_argument("ISD channel starts at one");
    return "/type=1num=" + std::to_string(channel) + "val=0work=0";
}

struct Instance {
    std::map<std::string, std::string> config;
    std::unique_ptr<IsdHttpTransport> transport;
    std::unique_ptr<IsdDriver> driver;
    unsigned defaultSwitchType = 2;
};

orbita_plugin_status_v1 create(const char*, const char* text, void** output,
                               orbita_plugin_buffer_v1* diagnostic)
{
    return plugin::guarded(diagnostic, [&] {
        if (!output) throw std::invalid_argument("Instance output pointer is required");
        auto instance = std::make_unique<Instance>();
        instance->config = plugin::arguments(text);
        const unsigned normalTimeout = plugin::unsignedValue(instance->config, "timeout_ms", 1500);
        instance->defaultSwitchType = plugin::unsignedValue(instance->config, "switch_type", 2);
        instance->transport = std::make_unique<IsdHttpTransport>(IsdHttpTransportConfig{
            plugin::required(instance->config, "host"),
            static_cast<std::uint16_t>(plugin::unsignedValue(instance->config, "port", 80)),
            normalTimeout});

        auto* transport = instance->transport.get();
        IsdDriverOps operations;
        operations.probe = [transport] {
            return transport->get("/").body;
        };
        operations.setSwitch = [transport](unsigned type, unsigned channel, bool enabled) {
            (void)transport->get(switchPath(type, channel, enabled));
        };
        operations.setAnalog = [transport](unsigned channel, unsigned value, bool enabled) {
            (void)transport->get(analogPath(channel, value, enabled));
        };
        operations.setYalkVoltage = [transport](unsigned channel, double volts) {
            (void)transport->get(yalkVoltagePath(channel, volts));
        };
        operations.disableYalkOutput = [transport](unsigned channel) {
            (void)transport->get(yalkOutputBusOffPath(channel));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            (void)transport->get(yalkOutputOffPath(channel));
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
            return std::string("status=ready\nalive=1\nconnectivity=http\nsession_state=")
                + toString(instance.driver->sessionState())
                + "\nglobal_hardware_state=not_readable\n"
                  "message=ИСД ответил по HTTP; состояние реле и внутренних UART не проверено\n"
                  "response=" + body + "\n";
        }
        if (command == "state" || command == "driver_state") {
            return instance.driver->statusText();
        }
        if (command == "trace") return instance.driver->traceText();
        if (command == "clear_trace") {
            instance.driver->clearTrace();
            return std::string("status=ok\noperation=clear_trace\n");
        }
        if (command == "release" || command == "release_owner") {
            instance.driver->releaseOwner(owner);
            return std::string("status=ok\noperation=release_owner\nowner=") + owner
                + "\nsession_state=" + toString(instance.driver->sessionState()) + "\n";
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

        if (command == "addressed_baseline") {
            const auto type3 = addressList(args, "type3_contacts");
            const auto type2 = addressList(args, "type2_contacts");
            const auto analog = addressList(args, "analog_type1_contacts");
            const bool performed = instance.driver->establishAddressedBaseline(
                type3, type2, analog,
                plugin::unsignedValue(args, "isd_command_gap_ms", 30), owner);
            return std::string("status=ok\noperation=addressed_baseline\nperformed=")
                + (performed ? "true" : "false") + "\nacknowledged_off_count="
                + std::to_string(type3.size() + type2.size() + analog.size()) + "\n";
        } else if (command == "recover_after_restart" || command == "recover") {
            // Explicit only: caller/operator must have restarted ISD first.
            // Driver probes and replays the exact process-owned desired state.
            instance.driver->recoverAfterRestart(owner);
        } else if (command == "yalk_set_voltage") {
            instance.driver->setYalkVoltage(
                resolvedChannel(), plugin::doubleValue(args, "volts"), owner);
        } else if (command == "yalk_output_off") {
            instance.driver->disableYalkOutput(resolvedChannel(), owner);
        } else if (command == "switch") {
            unsigned type = plugin::unsignedValue(args, "type", instance.defaultSwitchType);
            if (args.count("route")) {
                type = plugin::unsignedValue(instance.config,
                    "route." + args.at("route") + ".type", type);
            }
            instance.driver->setSwitch(
                type, resolvedChannel(), plugin::booleanValue(args, "enabled"), owner);
        } else if (command == "analog") {
            const unsigned analogValue = args.count("code")
                ? plugin::unsignedValue(args, "code")
                : plugin::unsignedValue(args, "value");
            if (args.count("route")) {
                const std::string prefix = "route." + args.at("route") + ".";
                const unsigned minimum = plugin::unsignedValue(instance.config, prefix + "min", 0);
                const unsigned maximum = plugin::unsignedValue(instance.config, prefix + "max", 4095);
                if (analogValue < minimum || analogValue > maximum) {
                    throw std::invalid_argument(
                        "Значение ИСД вне разрешённого диапазона маршрута " + args.at("route"));
                }
            }
            instance.driver->setAnalog(
                resolvedChannel(), analogValue,
                plugin::booleanValue(args, "enabled"), owner);
        } else {
            throw std::invalid_argument("Unsupported ISD operation: " + command);
        }

        return std::string("status=ok\noperation=") + command
            + "\nowner=" + owner
            + "\nsession_state=" + toString(instance.driver->sessionState()) + "\n";
    });
}

void cancel(void*) {}

void safeStop(void* value)
{
    if (!value) return;
    auto& instance = *static_cast<Instance*>(value);
    // Targeted only: never issue firmware type=4 from generic lifecycle cleanup.
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

} // namespace

extern "C" ORBITA_PLUGIN_EXPORT const orbita_equipment_api_v1* orbita_plugin_get_api_v1(void)
{
    return &api;
}
