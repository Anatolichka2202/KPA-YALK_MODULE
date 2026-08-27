#include "plugin_support.h"
#include "orbita_stand/equipment_adapters.h"

#include <atomic>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>

namespace {
using namespace orbita::stand;

struct Instance {
    std::map<std::string, std::string> config;
    std::unique_ptr<UbsiUdpAdapter> adapter;
    std::atomic_bool cancelled{false};
};

orbita_plugin_status_v1 create(
    const char*, const char* configText, void** output, orbita_plugin_buffer_v1* diagnostic)
{
    return plugin::guarded(diagnostic, [&] {
        if (!output) throw std::invalid_argument("Instance output pointer is required");
        auto instance = std::make_unique<Instance>();
        instance->config = plugin::arguments(configText);
        instance->adapter = std::make_unique<UbsiUdpAdapter>(UbsiUdpConfig{
            plugin::required(instance->config, "host"),
            instance->config["local_address"],
            static_cast<std::uint16_t>(plugin::unsignedValue(instance->config, "data_port", 1001)),
            static_cast<std::uint16_t>(plugin::unsignedValue(instance->config, "ack_port", 1101)),
            plugin::unsignedValue(instance->config, "timeout_ms", 800)});
        *output = instance.release();
        return std::string("UBSI Ethernet adapter created");
    });
}

void destroy(void* value) { delete static_cast<Instance*>(value); }

orbita_plugin_status_v1 invoke(
    void* value, const char* capability, const char* operation, const char* request,
    orbita_plugin_buffer_v1* response)
{
    return plugin::guarded(response, [&] {
        auto& instance = *static_cast<Instance*>(value);
        if (!capability || std::string(capability) != "ubsi.parameter_source") {
            throw std::invalid_argument("Unsupported capability");
        }
        const auto args = plugin::arguments(request);
        const std::string command = operation ? operation : "";
        if (command == "probe" || command == "alive") {
            return instance.adapter->waitForPassivePacket()
                ? std::string("status=ready\nmessage=Получен UDP-пакет УБСИ\n")
                : std::string("status=no_data\nmessage=За время ожидания пакет УБСИ не получен\n");
        }
        if (command == "select_mode") {
            plugin::requireActiveOutputs(instance.config);
            instance.adapter->selectMode(
                static_cast<std::uint8_t>(plugin::unsignedValue(args, "mode")),
                plugin::booleanValue(args, "single"));
            return std::string("status=ok\n");
        }
        if (command == "read_yalk") {
            const unsigned count = std::max(1u, plugin::unsignedValue(args, "sample_count", 16));
            unsigned wordIndex = plugin::unsignedValue(args, "word_index",
                std::max(1u, plugin::unsignedValue(args, "channel", 1)) - 1);
            unsigned configuredMask = plugin::unsignedValue(args, "mask", 0xFFFF);
            if (args.count("parameter_key")) {
                const std::string prefix = "parameter." + args.at("parameter_key") + ".";
                if (!instance.config.count(prefix + "word_base")) {
                    throw std::invalid_argument("Нет адресной привязки параметра " + args.at("parameter_key"));
                }
                wordIndex = plugin::unsignedValue(instance.config, prefix + "word_base")
                    + plugin::unsignedValue(args, "offset", 0);
                configuredMask = plugin::unsignedValue(instance.config, prefix + "mask", configuredMask);
            }
            const auto mask = static_cast<std::uint16_t>(configuredMask);
            if (wordIndex >= 100) throw std::invalid_argument("YALK word_index must be below 100");
            double sum = 0.0;
            for (unsigned index = 0; index < count; ++index) {
                if (instance.cancelled.load()) throw std::runtime_error("Operation cancelled");
                const auto bytes = instance.adapter->receiveRawPacket();
                const auto words = UbsiUdpAdapter::decodeYalkPacket(bytes, mask);
                sum += words[wordIndex];
            }
            std::ostringstream result;
            result << std::setprecision(15) << "raw=" << sum / count << "\nsamples=" << count << "\n";
            return result.str();
        }
        if (command == "read_packet") {
            const auto bytes = instance.adapter->receiveRawPacket();
            std::ostringstream result;
            result << "size=" << bytes.size() << "\nhex=" << std::hex << std::setfill('0');
            for (const auto byte : bytes) result << std::setw(2) << static_cast<unsigned>(byte);
            result << '\n';
            return result.str();
        }
        throw std::invalid_argument("Unsupported UBSI adapter operation: " + command);
    });
}

void cancel(void* value) { if (value) static_cast<Instance*>(value)->cancelled.store(true); }
void safeStop(void*) {}

const orbita_equipment_api_v1 api{
    ORBITA_EQUIPMENT_ABI_V1, sizeof(orbita_equipment_api_v1),
    "orbita.ubsi_udp", "Адаптер УБСИ Ethernet/RS-485", "ubsi.parameter_source",
    create, destroy, invoke, cancel, safeStop};
}

extern "C" ORBITA_PLUGIN_EXPORT const orbita_equipment_api_v1* orbita_plugin_get_api_v1(void)
{
    return &api;
}
