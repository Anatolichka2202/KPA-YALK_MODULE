#include "plugin_support.h"
#include "orbita_stand/dho_waveform.h"
#include "orbita_stand/visa_instrument.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>

namespace {
using namespace orbita::stand;
struct Instance { std::unique_ptr<VisaInstrument> scope; };
orbita_plugin_status_v1 create(const char*, const char* text, void** output, orbita_plugin_buffer_v1* diagnostic)
{
    return plugin::guarded(diagnostic, [&] {
        if (!output) throw std::invalid_argument("Instance output pointer is required");
        const auto values = plugin::arguments(text);
        VisaInstrumentConfig config{{values.count("resource") ? values.at("resource")
            : "USB[0-9]*::0x1AB1::?*::?*INSTR"}, plugin::unsignedValue(values, "timeout_ms", 4000)};
        auto instance = std::make_unique<Instance>();
        instance->scope = std::make_unique<VisaInstrument>(std::move(config));
        *output = instance.release();
        return std::string("Rigol DHO8xx VISA session created");
    });
}
void destroy(void* value) { delete static_cast<Instance*>(value); }
orbita_plugin_status_v1 invoke(void* value, const char* capability, const char* operation,
                              const char* request, orbita_plugin_buffer_v1* response)
{
    return plugin::guarded(response, [&] {
        auto& instance = *static_cast<Instance*>(value);
        if (!capability || std::string(capability) != "measure.waveform") throw std::invalid_argument("Unsupported capability");
        const std::string command = operation ? operation : "";
        const auto args = plugin::arguments(request);
        if (command == "probe") return "idn=" + instance.scope->query("*IDN?")
            + "resource=" + instance.scope->resourceName() + "\n";
        const unsigned channel = plugin::unsignedValue(args, "channel", 1);
        if (channel < 1 || channel > 4) throw std::invalid_argument("DHO8xx channel must be 1..4");
        if (command == "configure") {
            instance.scope->write(":CHAN" + std::to_string(channel) + ":SCAL "
                + std::to_string(plugin::doubleValue(args, "vertical_scale_v", 1.0)));
            instance.scope->write(":TIM:SCAL " + std::to_string(plugin::doubleValue(args, "time_scale_s", 0.001)));
            instance.scope->write(":TRIG:EDGE:SOUR CHAN" + std::to_string(channel));
            instance.scope->write(":TRIG:EDGE:LEV " + std::to_string(plugin::doubleValue(args, "trigger_level_v", 0.0)));
            return std::string("status=ok\n");
        }
        if (command == "single") {
            instance.scope->write(":SING");
            return std::string("status=ok\n");
        }
        if (command == "capture") {
            instance.scope->write(":WAV:SOUR CHAN" + std::to_string(channel));
            instance.scope->write(":WAV:MODE NORM");
            instance.scope->write(":WAV:FORM WORD");
            const auto preamble = instance.scope->query(":WAV:PRE?");
            const auto raw = instance.scope->queryRaw(":WAV:DATA?", plugin::unsignedValue(args, "max_bytes", 4 * 1024 * 1024));
            const auto waveform = decodeDhoWordWaveform(preamble, raw);
            const auto [minimum, maximum] = std::minmax_element(waveform.volts.begin(), waveform.volts.end());
            const double average = std::accumulate(waveform.volts.begin(), waveform.volts.end(), 0.0)
                / static_cast<double>(waveform.volts.size());
            std::ostringstream result;
            result << std::setprecision(15) << "points=" << waveform.volts.size()
                << "\nminimum_v=" << *minimum << "\nmaximum_v=" << *maximum
                << "\naverage_v=" << average << "\nwaveform_csv=time_s,voltage_v";
            for (std::size_t index = 0; index < waveform.volts.size(); ++index) {
                result << '\n' << waveform.seconds[index] << ',' << waveform.volts[index];
            }
            result << '\n';
            return result.str();
        }
        throw std::invalid_argument("Unsupported DHO8xx operation: " + command);
    });
}
void cancel(void*) {}
void safeStop(void*) {}
const orbita_equipment_api_v1 api{ORBITA_EQUIPMENT_ABI_V1, sizeof(orbita_equipment_api_v1),
    "orbita.rigol_dho8xx", "Осциллограф Rigol DHO8xx", "measure.waveform",
    create, destroy, invoke, cancel, safeStop};
}
extern "C" ORBITA_PLUGIN_EXPORT const orbita_equipment_api_v1* orbita_plugin_get_api_v1(void) { return &api; }
