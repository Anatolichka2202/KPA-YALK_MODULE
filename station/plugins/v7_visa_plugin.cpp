#include "plugin_support.h"
#include "orbita_stand/v7_visa_voltmeter.h"

#include <iomanip>
#include <memory>
#include <sstream>

namespace {
using namespace orbita::stand;
struct Instance { std::unique_ptr<V7VisaVoltmeter> meter; };
orbita_plugin_status_v1 create(const char*, const char* text, void** output, orbita_plugin_buffer_v1* diagnostic)
{
    return plugin::guarded(diagnostic, [&] {
        if (!output) throw std::invalid_argument("Instance output pointer is required");
        const auto config = plugin::arguments(text);
        V7VisaConfig value;
        if (config.count("resource")) value.resourceExpression = config.at("resource");
        if (config.count("fallback_resource")) value.fallbackResourceExpression = config.at("fallback_resource");
        value.timeoutMilliseconds = plugin::unsignedValue(config, "timeout_ms", value.timeoutMilliseconds);
        value.readDelayMilliseconds = plugin::unsignedValue(config, "read_delay_ms", value.readDelayMilliseconds);
        if (config.count("voltage_command")) value.voltageCommand = config.at("voltage_command");
        if (config.count("current_command")) value.currentCommand = config.at("current_command");
        if (config.count("ac_voltage_command")) value.acVoltageCommand = config.at("ac_voltage_command");
        if (config.count("frequency_command")) value.frequencyCommand = config.at("frequency_command");
        auto instance = std::make_unique<Instance>();
        instance->meter = std::make_unique<V7VisaVoltmeter>(std::move(value));
        *output = instance.release();
        return std::string("V7-78/1 VISA session created");
    });
}
void destroy(void* value) { delete static_cast<Instance*>(value); }
orbita_plugin_status_v1 invoke(void* value, const char* capability, const char* operation,
                              const char*, orbita_plugin_buffer_v1* response)
{
    return plugin::guarded(response, [&] {
        auto& instance = *static_cast<Instance*>(value);
        if (!capability || (std::string(capability) != "measure.reference_voltage"
            && std::string(capability) != "measure.dc_current"
            && std::string(capability) != "measure.reference_ac_voltage"
            && std::string(capability) != "measure.reference_frequency")) throw std::invalid_argument("Unsupported capability");
        const std::string command = operation ? operation : "";
        if (command != "probe" && command != "read_voltage" && command != "read_current"
            && command != "read_ac_voltage" && command != "read_frequency") {
            throw std::invalid_argument("Unsupported V7 operation");
        }
        std::ostringstream result;
        result << std::setprecision(15) << "resource=" << instance.meter->resourceName();
        if (command == "read_current") result << "\namperes=" << instance.meter->readCurrent();
        else if (command == "read_frequency") result << "\nhertz=" << instance.meter->readFrequency();
        else if (command == "read_ac_voltage") result << "\nvolts=" << instance.meter->readAcVoltage();
        else result << "\nvolts=" << instance.meter->readVoltage();
        result << "\n";
        return result.str();
    });
}
void cancel(void*) {}
void safeStop(void*) {}
const orbita_equipment_api_v1 api{ORBITA_EQUIPMENT_ABI_V1, sizeof(orbita_equipment_api_v1),
    "orbita.v7_visa", "Вольтметр В7-78/1", "measure.reference_voltage;measure.dc_current;measure.reference_ac_voltage;measure.reference_frequency",
    create, destroy, invoke, cancel, safeStop};
}
extern "C" ORBITA_PLUGIN_EXPORT const orbita_equipment_api_v1* orbita_plugin_get_api_v1(void) { return &api; }
