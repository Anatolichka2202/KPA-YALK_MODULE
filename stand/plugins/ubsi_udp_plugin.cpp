#include "plugin_support.h"
#include "orbita_stand/equipment_adapters.h"

#include <atomic>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>
#include <cctype>
#include <thread>

namespace {
using namespace orbita::stand;

struct Instance {
    std::map<std::string, std::string> config;
    std::unique_ptr<UbsiUdpAdapter> adapter;
    std::atomic_bool cancelled{false};
    int selectedMode = -1;
    std::string protocol;
};

std::string prepareRokotYalk(Instance& instance, const std::map<std::string, std::string>& args)
{
    const auto adapterChannel = static_cast<std::uint8_t>(
        plugin::unsignedValue(args, "adapter_channel", 1));
    const auto addressCount = static_cast<std::uint8_t>(
        plugin::unsignedValue(args, "address_count", 43));
    const auto yalkNumber = static_cast<std::uint8_t>(
        plugin::unsignedValue(args, "yalk_number", 1));
    const bool slow = plugin::booleanValue(args, "slow", true);
    const bool fast = plugin::booleanValue(args, "fast", false);

    instance.adapter->sendRawCommand(UbsiUdpAdapter::rokotResetCommand());
    instance.adapter->sendRawCommand(UbsiUdpAdapter::rokotConfigureYalkCommand(
        adapterChannel, addressCount, slow, fast));
    instance.adapter->sendRawCommand(UbsiUdpAdapter::rokotConfigureYtpCommand());
    instance.adapter->sendRawCommand(UbsiUdpAdapter::rokotSelectYalkCommand(yalkNumber));

    // Подтверждение — не архивное echo, а возобновившийся рабочий кадр ЯЛК:
    // 02 <номер-1> <число адресов> 00 + 100 little-endian слов.
    const auto frame = instance.adapter->receiveRawPacket();
    if (frame.size() != 204 || frame[0] != 0x02 || frame[1] != yalkNumber - 1
        || frame[2] != addressCount) {
        throw std::runtime_error("Адаптер не подтвердил рабочий поток ЯЛК после настройки ROKOT");
    }
    instance.selectedMode = 6;
    std::ostringstream result;
    result << "status=ready\nprotocol=kpa_rokot_udp\nmode=6\nadapter_channel="
           << static_cast<unsigned>(adapterChannel) << "\nyalk_number="
           << static_cast<unsigned>(yalkNumber) << "\naddress_count="
           << static_cast<unsigned>(addressCount) << "\nslow=" << (slow ? "true" : "false")
           << "\nfast=" << (fast ? "true" : "false") << "\n";
    return result.str();
}

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
            if (requestedMode < 0 || requestedMode > 30) {
                throw std::invalid_argument("Режим адаптера должен быть в диапазоне 0..30");
            }
            if (instance.protocol == "kpa_rokot_udp") {
                if (requestedMode != 6) {
                    throw std::invalid_argument(
                        "Для рабочего протокола ROKOT подтверждён только режим ЯЛК 8 кГц (6)");
                }
                if (instance.selectedMode != requestedMode) return prepareRokotYalk(instance, args);
            } else if (instance.selectedMode != requestedMode
                       || plugin::booleanValue(args, "single")) {
                instance.adapter->selectMode(static_cast<std::uint8_t>(requestedMode),
                    plugin::booleanValue(args, "single"));
                instance.selectedMode = requestedMode;
            }
            return std::string("status=ok\nmode=") + std::to_string(requestedMode)
                + "\nsingle=" + (plugin::booleanValue(args, "single") ? "true" : "false") + "\n";
        }
        if (command == "prepare_yalk") {
            plugin::requireActiveOutputs(instance.config);
            if (instance.protocol != "kpa_rokot_udp") {
                throw std::invalid_argument("prepare_yalk доступна только для kpa_rokot_udp");
            }
            return prepareRokotYalk(instance, args);
        }
        if (command == "reset_adapter" || command == "configure_yalk"
            || command == "configure_ytp" || command == "select_yalk") {
            plugin::requireActiveOutputs(instance.config);
            if (instance.protocol != "kpa_rokot_udp") {
                throw std::invalid_argument("Операция ROKOT недоступна для архивного протокола");
            }
            if (command == "reset_adapter") {
                instance.adapter->sendRawCommand(UbsiUdpAdapter::rokotResetCommand());
                instance.selectedMode = -1;
            } else if (command == "configure_yalk") {
                instance.adapter->sendRawCommand(UbsiUdpAdapter::rokotConfigureYalkCommand(
                    static_cast<std::uint8_t>(plugin::unsignedValue(args, "adapter_channel", 1)),
                    static_cast<std::uint8_t>(plugin::unsignedValue(args, "address_count", 43)),
                    plugin::booleanValue(args, "slow", true),
                    plugin::booleanValue(args, "fast", false)));
            } else if (command == "configure_ytp") {
                instance.adapter->sendRawCommand(UbsiUdpAdapter::rokotConfigureYtpCommand(
                    static_cast<std::uint8_t>(plugin::unsignedValue(args, "adapter_channel", 1)),
                    static_cast<std::uint8_t>(plugin::unsignedValue(args, "first_address", 1)),
                    static_cast<std::uint8_t>(plugin::unsignedValue(args, "address_count", 1))));
            } else {
                instance.adapter->sendRawCommand(UbsiUdpAdapter::rokotSelectYalkCommand(
                    static_cast<std::uint8_t>(plugin::unsignedValue(args, "yalk_number", 1))));
                instance.selectedMode = 6;
            }
            return "status=sent\nprotocol=kpa_rokot_udp\noperation=" + command + "\n";
        }
        if (command == "read" || command == "read_yalk") {
            const unsigned count = std::max(1u, plugin::unsignedValue(args, "sample_count", 16));
            unsigned wordIndex = plugin::unsignedValue(args, "word_index",
                std::max(1u, plugin::unsignedValue(args, "channel", 1)) - 1);
            if (args.count("ulk_address")) {
                wordIndex = UbsiUdpAdapter::wordIndexForUlkAddress(
                    plugin::unsignedValue(args, "ulk_address"));
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
        if (command == "read_frame") {
            // Инженерская операция: только приём уже идущего потока.  Она не
            // выбирает режим адаптера и не посылает команд в стенд.
            const auto bytes = instance.adapter->receiveRawPacket();
            const auto words = UbsiUdpAdapter::decodeYalkPacket(bytes, 0xFFFF);
            std::ostringstream result;
            result << "status=ready\nsize=" << bytes.size() << "\nheader=";
            const std::size_t headerSize = bytes.size() == 204 ? 4 : 0;
            result << std::hex << std::setfill('0');
            for (std::size_t index = 0; index < headerSize; ++index)
                result << std::setw(2) << static_cast<unsigned>(bytes[index]);
            result << std::dec << "\nwords=";
            for (std::size_t index = 0; index < words.size(); ++index) {
                if (index) result << ',';
                result << words[index];
            }
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
