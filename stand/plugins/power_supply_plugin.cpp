#include "plugin_support.h"
#include "orbita_stand/visa_instrument.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <memory>
#include <sstream>

namespace {
using namespace orbita::stand;

struct Supply {
    std::string role;
    std::unique_ptr<VisaInstrument> instrument;
    std::string identity;
};
struct Instance { std::map<std::string, std::string> config; Supply ni; Supply bi; };

std::string setting(const std::map<std::string, std::string>& config,
                    const std::string& key, const std::string& fallback)
{
    const auto found = config.find(key);
    return found == config.end() || found->second.empty() ? fallback : found->second;
}

std::string withValue(std::string pattern, double value)
{
    std::ostringstream text;
    text << std::setprecision(10) << value;
    const auto position = pattern.find("{value}");
    if (position == std::string::npos) return pattern + " " + text.str();
    pattern.replace(position, 7, text.str());
    return pattern;
}

double queryNumber(Supply& supply, const std::string& query, unsigned delay)
{
    const auto response = supply.instrument->query(query, delay);
    std::size_t parsed = 0;
    const double value = std::stod(response, &parsed);
    if (!parsed || !std::isfinite(value)) throw std::runtime_error(
        "АКИП " + supply.role + " вернул некорректное число");
    return value;
}

Supply openSupply(const std::map<std::string, std::string>& config, const char* role)
{
    const auto resource = plugin::required(config, std::string("resource_") + role);
    Supply result;
    result.role = role;
    result.instrument = std::make_unique<VisaInstrument>(VisaInstrumentConfig{
        {resource}, plugin::unsignedValue(config, "timeout_ms", 2000)});
    result.identity = result.instrument->query(setting(config, "idn_command", "*IDN?"));
    const auto expected = setting(config, "expected_idn", "AKIP-1160/6");
    std::string actualUpper = result.identity;
    std::string expectedUpper = expected;
    std::transform(actualUpper.begin(), actualUpper.end(), actualUpper.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    std::transform(expectedUpper.begin(), expectedUpper.end(), expectedUpper.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (!expectedUpper.empty() && actualUpper.find(expectedUpper) == std::string::npos) {
        throw std::runtime_error("Роль " + std::string(role)
            + " назначена не АКИП-1160/6: " + result.identity);
    }
    return result;
}

orbita_plugin_status_v1 create(const char*, const char* text, void** output,
                               orbita_plugin_buffer_v1* diagnostic)
{
    return plugin::guarded(diagnostic, [&] {
        if (!output) throw std::invalid_argument("Instance output pointer is required");
        auto instance = std::make_unique<Instance>();
        instance->config = plugin::arguments(text);
        instance->ni = openSupply(instance->config, "ni");
        instance->bi = openSupply(instance->config, "bi");
        *output = instance.release();
        return std::string("Пара АКИП-1160/6 открыта: ипНИ и ипБИ");
    });
}
void destroy(void* value) { delete static_cast<Instance*>(value); }

orbita_plugin_status_v1 invoke(void* value, const char* capability, const char* operation,
                              const char* request, orbita_plugin_buffer_v1* response)
{
    return plugin::guarded(response, [&] {
        auto& instance = *static_cast<Instance*>(value);
        if (!capability || std::string(capability) != "power.dc_supply")
            throw std::invalid_argument("Unsupported capability");
        const std::string action = operation ? operation : "";
        const auto args = plugin::arguments(request);
        const unsigned delay = plugin::unsignedValue(instance.config, "read_delay_ms", 60);
        if (action == "probe") {
            return "status=ready\nresource_ni=" + instance.ni.instrument->resourceName()
                + "\nidn_ni=" + instance.ni.identity
                + "\nresource_bi=" + instance.bi.instrument->resourceName()
                + "\nidn_bi=" + instance.bi.identity + "\n";
        }
        if (action == "read_state") {
            const auto voltageQuery = setting(instance.config, "measure_voltage_command", "MEAS:VOLT?");
            const auto currentQuery = setting(instance.config, "measure_current_command", "MEAS:CURR?");
            const double voltsNi = queryNumber(instance.ni, voltageQuery, delay);
            const double voltsBi = queryNumber(instance.bi, voltageQuery, delay);
            const double currentNi = queryNumber(instance.ni, currentQuery, delay);
            const double currentBi = queryNumber(instance.bi, currentQuery, delay);
            std::ostringstream result;
            result << std::setprecision(12) << "status=ready\nvolts=" << (voltsNi + voltsBi) / 2.0
                   << "\namperes=" << currentNi + currentBi << "\nvolts_ni=" << voltsNi
                   << "\nvolts_bi=" << voltsBi << "\namperes_ni=" << currentNi
                   << "\namperes_bi=" << currentBi << "\n";
            return result.str();
        }
        plugin::requireActiveOutputs(instance.config);
        if (action == "set_voltage" || action == "set_current_limit") {
            const double target = plugin::doubleValue(args,
                action == "set_voltage" ? "volts" : "amperes");
            const auto pattern = setting(instance.config,
                action == "set_voltage" ? "set_voltage_command" : "set_current_command",
                action == "set_voltage" ? "VOLT {value}" : "CURR {value}");
            instance.ni.instrument->write(withValue(pattern, target));
            instance.bi.instrument->write(withValue(pattern, target));
        } else if (action == "output") {
            const bool enabled = plugin::booleanValue(args, "enabled");
            const auto outputCommand = setting(instance.config,
                enabled ? "output_on_command" : "output_off_command",
                enabled ? "OUTP ON" : "OUTP OFF");
            instance.ni.instrument->write(outputCommand);
            instance.bi.instrument->write(outputCommand);
        } else throw std::invalid_argument("Unsupported AKIP operation: " + action);
        return std::string("status=ok\n");
    });
}
void cancel(void*) {}
void safeStop(void* value)
{
    if (!value) return;
    auto& instance = *static_cast<Instance*>(value);
    if (!plugin::booleanValue(instance.config, "profile.active_outputs_confirmed")
        && !plugin::booleanValue(instance.config, "device.active_commands_confirmed")) return;
    const auto off = setting(instance.config, "output_off_command", "OUTP OFF");
    try { instance.ni.instrument->write(off); } catch (...) {}
    try { instance.bi.instrument->write(off); } catch (...) {}
}

const orbita_equipment_api_v1 api{ORBITA_EQUIPMENT_ABI_V1, sizeof(orbita_equipment_api_v1),
    "orbita.akip_1160_pair", "Пара источников АКИП-1160/6 (ипНИ/ипБИ)", "power.dc_supply",
    create, destroy, invoke, cancel, safeStop};
}
extern "C" ORBITA_PLUGIN_EXPORT const orbita_equipment_api_v1* orbita_plugin_get_api_v1(void)
{
    return &api;
}
