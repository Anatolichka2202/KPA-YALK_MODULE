#include "plugin_support.h"
#include "orbita_stand/equipment_adapters.h"

#include <atomic>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>
#include <cctype>

namespace {
using namespace orbita::stand;

struct Instance {
    std::map<std::string, std::string> config;
    std::unique_ptr<UbsiUdpAdapter> adapter;
    std::atomic_bool cancelled{false};
    int selectedMode = -1;
    std::string protocol;
};

std::vector<std::uint8_t> hexBytes(const std::string& text)
{
    std::string compact;
    for (unsigned char character : text) {
        if (!std::isspace(character) && character != ':' && character != '-')
            compact.push_back(static_cast<char>(character));
    }
    if (compact.empty() || compact.size() % 2) throw std::invalid_argument(
        "KPA command hex must contain whole bytes");
    std::vector<std::uint8_t> result;
    for (std::size_t offset = 0; offset < compact.size(); offset += 2) {
        std::size_t parsed = 0;
        const auto value = std::stoul(compact.substr(offset, 2), &parsed, 16);
        if (parsed != 2) throw std::invalid_argument("Invalid byte in KPA command hex");
        result.push_back(static_cast<std::uint8_t>(value));
    }
    return result;
}

orbita_plugin_status_v1 create(
    const char*, const char* configText, void** output, orbita_plugin_buffer_v1* diagnostic)
{
    return plugin::guarded(diagnostic, [&] {
        if (!output) throw std::invalid_argument("Instance output pointer is required");
        auto instance = std::make_unique<Instance>();
        instance->config = plugin::arguments(configText);
        instance->protocol = instance->config.count("protocol")
            ? instance->config.at("protocol") : "ktma_firmware_udp_v1";
        if (instance->protocol != "ktma_firmware_udp_v1"
            && instance->protocol != "kpa_rokot_udp") {
            throw std::invalid_argument("Неизвестный протокол адаптера: " + instance->protocol);
        }
        instance->adapter = std::make_unique<UbsiUdpAdapter>(UbsiUdpConfig{
            plugin::required(instance->config, "host"),
            instance->config["local_address"],
            static_cast<std::uint16_t>(plugin::unsignedValue(instance->config, "data_port", 1001)),
            static_cast<std::uint16_t>(plugin::unsignedValue(instance->config, "ack_port", 1101)),
            plugin::unsignedValue(instance->config, "timeout_ms", 800),
            plugin::booleanValue(instance->config, "accept_any_sender")});
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
                ? std::string("status=ready\nalive=1\nprotocol=") + instance.protocol
                    + "\nmessage=Получен UDP-пакет УБСИ\n"
                : std::string("status=no_data\nalive=0\nprotocol=") + instance.protocol
                    + "\nmessage=За время ожидания пакет УБСИ не получен\n";
        }
        if (command == "select_mode") {
            plugin::requireActiveOutputs(instance.config);
            const int requestedMode = static_cast<int>(plugin::unsignedValue(args, "mode"));
            if (instance.selectedMode != requestedMode || plugin::booleanValue(args, "single")) {
                if (instance.protocol == "ktma_firmware_udp_v1") {
                    instance.adapter->selectMode(static_cast<std::uint8_t>(requestedMode),
                        plugin::booleanValue(args, "single"));
                } else {
                    const std::string key = "kpa.mode." + std::to_string(requestedMode) + ".hex";
                    if (!instance.config.count(key) || instance.config.at(key).empty()
                        || instance.config.at(key).find("TODO") != std::string::npos) {
                        throw std::runtime_error("Для kpa_rokot_udp не записана захваченная команда режима "
                            + std::to_string(requestedMode));
                    }
                    instance.adapter->sendRawCommand(hexBytes(instance.config.at(key)));
                }
                instance.selectedMode = requestedMode;
            }
            return std::string("status=ok\n");
        }
        if (command == "read" || command == "read_yalk") {
            const unsigned count = std::max(1u, plugin::unsignedValue(args, "sample_count", 16));
            unsigned wordIndex = plugin::unsignedValue(args, "word_index",
                std::max(1u, plugin::unsignedValue(args, "channel", 1)) - 1);
            if (args.count("ulk_address")) {
                const std::string mapKey = "ulk."
                    + plugin::required(args, "parameter_group") + ".word."
                    + args.at("ulk_address");
                if (!instance.config.count(mapKey)) {
                    throw std::runtime_error("Адрес УЛК " + args.at("ulk_address")
                        + " известен из KPA, но его позиция в пакете текущего протокола не подтверждена");
                }
                wordIndex = plugin::unsignedValue(instance.config, mapKey);
            }
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
