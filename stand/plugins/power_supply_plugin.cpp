#include "plugin_support.h"
#include "orbita_stand/equipment_adapters.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

namespace {
using namespace orbita::stand;

struct Supply {
    std::string role;
    std::unique_ptr<Akip1160Serial> instrument;
    std::string identity;
};

struct Instance {
    std::map<std::string, std::string> config;
    std::vector<Supply> supplies;
    bool voltageArmed = false;
    bool currentArmed = false;
    double requestedVoltage = 0.0;
    double requestedTotalCurrentLimit = 0.0;
};

std::string setting(const std::map<std::string, std::string>& config,
                    const std::string& key, const std::string& fallback = {})
{
    const auto found = config.find(key);
    return found == config.end() || found->second.empty() ? fallback : found->second;
}

std::string normalizePort(std::string value)
{
    if (value.rfind("ASRL", 0) == 0) {
        const auto separator = value.find("::");
        const auto number = value.substr(4, separator == std::string::npos
            ? std::string::npos : separator - 4);
        if (!number.empty() && std::all_of(number.begin(), number.end(),
                [](unsigned char item) { return std::isdigit(item) != 0; })) {
            return "COM" + number;
        }
    }
    return value;
}

std::string rolePort(const std::map<std::string, std::string>& config, const std::string& role)
{
    auto port = setting(config, "port_" + role);
    if (port.empty()) port = setting(config, "resource_" + role);
    if (port.empty() && role == "ni") port = setting(config, "port");
    return normalizePort(port);
}

std::string uppercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char item) { return static_cast<char>(std::toupper(item)); });
    return value;
}

Supply openSupply(const std::map<std::string, std::string>& config,
                  const std::string& role, const std::string& port)
{
    Supply result;
    result.role = role;
    result.instrument = std::make_unique<Akip1160Serial>(Akip1160SerialConfig{
        port,
        static_cast<int>(plugin::unsignedValue(config, "baud", 115200)),
        plugin::unsignedValue(config, "timeout_ms", 1200)});
    result.identity = result.instrument->identity();
    const auto expected = uppercase(setting(config, "expected_idn", "AKIP-1160/6"));
    if (!expected.empty() && uppercase(result.identity).find(expected) == std::string::npos) {
        throw std::runtime_error("Роль " + role + " назначена не АКИП-1160/6: "
            + result.identity);
    }
    return result;
}

void outputsOff(Instance& instance) noexcept
{
    for (auto& supply : instance.supplies) {
        try { supply.instrument->setOutput(false); } catch (...) {}
    }
}

void verifyNear(double actual, double expected, double tolerance, const std::string& what)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error("АКИП не подтвердил " + what + ": задано "
            + std::to_string(expected) + ", прочитано " + std::to_string(actual));
    }
}

std::string stateText(Instance& instance)
{
    double measuredVoltage = 0.0;
    double measuredCurrent = 0.0;
    double setVoltage = 0.0;
    double setCurrent = 0.0;
    bool allOutputsEnabled = !instance.supplies.empty();
    std::ostringstream details;
    details << std::setprecision(12);
    for (auto& supply : instance.supplies) {
        const double voltage = supply.instrument->measuredVoltage();
        const double current = supply.instrument->measuredCurrent();
        const double voltageSetpoint = supply.instrument->voltageSetpoint();
        const double currentSetpoint = supply.instrument->currentSetpoint();
        const bool enabled = supply.instrument->outputEnabled();
        measuredVoltage += voltage;
        measuredCurrent += current;
        setVoltage += voltageSetpoint;
        setCurrent += currentSetpoint;
        allOutputsEnabled = allOutputsEnabled && enabled;
        details << "port_" << supply.role << '=' << supply.instrument->portName() << '\n'
                << "volts_" << supply.role << '=' << voltage << '\n'
                << "amperes_" << supply.role << '=' << current << '\n'
                << "set_volts_" << supply.role << '=' << voltageSetpoint << '\n'
                << "set_amperes_" << supply.role << '=' << currentSetpoint << '\n'
                << "output_" << supply.role << '=' << (enabled ? "on" : "off") << '\n';
    }
    const double count = static_cast<double>(instance.supplies.size());
    std::ostringstream result;
    result << std::setprecision(12)
           << "status=ready\nsupply_count=" << instance.supplies.size()
           << "\nvolts=" << measuredVoltage / count
           << "\namperes=" << measuredCurrent
           << "\nset_volts=" << setVoltage / count
           << "\nset_amperes_total=" << setCurrent
           << "\noutput_enabled=" << (allOutputsEnabled ? "true" : "false") << '\n'
           << details.str();
    return result.str();
}

orbita_plugin_status_v1 create(const char*, const char* text, void** output,
                               orbita_plugin_buffer_v1* diagnostic)
{
    return plugin::guarded(diagnostic, [&] {
        if (!output) throw std::invalid_argument("Instance output pointer is required");
        auto instance = std::make_unique<Instance>();
        instance->config = plugin::arguments(text);
        const auto niPort = rolePort(instance->config, "ni");
        if (niPort.empty()) throw std::invalid_argument("Missing argument: port_ni");
        instance->supplies.push_back(openSupply(instance->config, "ni", niPort));
        const auto biPort = rolePort(instance->config, "bi");
        if (!biPort.empty()) instance->supplies.push_back(openSupply(instance->config, "bi", biPort));
        if (plugin::booleanValue(instance->config, "require_both")
            && instance->supplies.size() != 2) {
            throw std::runtime_error("Профиль требует оба АКИП: ипНИ и ипБИ");
        }
        const auto count = instance->supplies.size();
        *output = instance.release();
        return std::string("АКИП-1160/6 идентифицирован, источников: ")
            + std::to_string(count);
    });
}

void destroy(void* value) { delete static_cast<Instance*>(value); }

orbita_plugin_status_v1 invoke(void* value, const char* capability, const char* operation,
                              const char* request, orbita_plugin_buffer_v1* response)
{
    return plugin::guarded(response, [&] {
        auto& instance = *static_cast<Instance*>(value);
        if (!capability || std::string(capability) != "power.dc_supply") {
            throw std::invalid_argument("Unsupported capability");
        }
        const std::string action = operation ? operation : "";
        const auto args = plugin::arguments(request);
        if (action == "probe") {
            std::ostringstream result;
            result << "status=ready\ntransport=usb_serial_scpi\nbaud="
                   << plugin::unsignedValue(instance.config, "baud", 115200) << '\n';
            for (const auto& supply : instance.supplies) {
                result << "port_" << supply.role << '=' << supply.instrument->portName() << '\n'
                       << "idn_" << supply.role << '=' << supply.identity << '\n';
            }
            result << stateText(instance);
            return result.str();
        }
        if (action == "read_state") return stateText(instance);

        if (action == "output" && !plugin::booleanValue(args, "enabled")) {
            outputsOff(instance);
            for (auto& supply : instance.supplies) {
                if (supply.instrument->outputEnabled()) {
                    throw std::runtime_error("АКИП не подтвердил отключение выхода " + supply.role);
                }
            }
            return std::string("status=ok\noutput_enabled=false\n");
        }

        plugin::requireActiveOutputs(instance.config);
        try {
            if (action == "set_voltage") {
                const double target = plugin::doubleValue(args, "volts");
                const double maximum = plugin::doubleValue(instance.config, "max_voltage_v", 60.0);
                if (!std::isfinite(target) || target < 0.0 || target > maximum) {
                    throw std::invalid_argument("Напряжение АКИП вне разрешённого профилем диапазона");
                }
                for (auto& supply : instance.supplies) supply.instrument->setVoltage(target);
                const double tolerance = plugin::doubleValue(
                    instance.config, "setpoint_voltage_tolerance_v", 0.011);
                for (auto& supply : instance.supplies) {
                    verifyNear(supply.instrument->voltageSetpoint(), target, tolerance,
                        "напряжение " + supply.role);
                }
                instance.requestedVoltage = target;
                instance.voltageArmed = true;
            } else if (action == "set_current_limit") {
                const double requestedTotal = plugin::doubleValue(args, "amperes");
                const double maximum = plugin::doubleValue(instance.config, "max_current_a", 10.0);
                const bool totalMode = plugin::booleanValue(
                    instance.config, "current_limit_is_total", true);
                const double perSupply = totalMode
                    ? requestedTotal / static_cast<double>(instance.supplies.size())
                    : requestedTotal;
                if (!std::isfinite(perSupply) || perSupply < 0.005 || perSupply > maximum) {
                    throw std::invalid_argument("Ограничение тока АКИП вне разрешённого профилем диапазона");
                }
                for (auto& supply : instance.supplies) supply.instrument->setCurrentLimit(perSupply);
                const double tolerance = plugin::doubleValue(
                    instance.config, "setpoint_current_tolerance_a", 0.002);
                for (auto& supply : instance.supplies) {
                    verifyNear(supply.instrument->currentSetpoint(), perSupply, tolerance,
                        "ограничение тока " + supply.role);
                }
                instance.requestedTotalCurrentLimit = requestedTotal;
                instance.currentArmed = true;
            } else if (action == "output") {
                if (!instance.voltageArmed || !instance.currentArmed) {
                    throw std::runtime_error(
                        "Включение АКИП запрещено: текущий запуск должен сначала задать напряжение и ограничение тока");
                }
                for (auto& supply : instance.supplies) supply.instrument->setOutput(true);
                for (auto& supply : instance.supplies) {
                    if (!supply.instrument->outputEnabled()) {
                        throw std::runtime_error("АКИП не подтвердил включение выхода " + supply.role);
                    }
                }
            } else {
                throw std::invalid_argument("Unsupported AKIP operation: " + action);
            }
        } catch (...) {
            outputsOff(instance);
            throw;
        }
        std::ostringstream result;
        result << std::setprecision(12) << "status=ok\nrequested_voltage="
               << instance.requestedVoltage << "\nrequested_total_current_limit="
               << instance.requestedTotalCurrentLimit << '\n';
        return result.str();
    });
}

void safeStop(void* value)
{
    if (value) outputsOff(*static_cast<Instance*>(value));
}

void cancel(void* value) { safeStop(value); }

const orbita_equipment_api_v1 api{
    ORBITA_EQUIPMENT_ABI_V1, sizeof(orbita_equipment_api_v1),
    "orbita.akip_1160_pair", "АКИП-1160/6 USB/COM (один или два источника)",
    "power.dc_supply", create, destroy, invoke, cancel, safeStop};
}

extern "C" ORBITA_PLUGIN_EXPORT const orbita_equipment_api_v1* orbita_plugin_get_api_v1(void)
{
    return &api;
}
