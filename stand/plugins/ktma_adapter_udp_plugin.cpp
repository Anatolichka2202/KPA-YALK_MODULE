#include "plugin_support.h"
#include "orbita_stand/ulk_udp_transport.h"
#include "orbita_stand/yalk_frame.h"

#include <atomic>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <memory>
#include <numeric>
#include <sstream>

namespace {
using namespace orbita::stand;

struct Instance {
    std::map<std::string, std::string> config;
    std::unique_ptr<UlkUdpTransport> transport;
    std::atomic_bool cancelled{false};
    int selectedMode = -1;
    bool yalkReference = false;
};

unsigned timeout(const Instance& instance, const std::map<std::string, std::string>& args)
{
    return plugin::unsignedValue(args, "timeout_ms",
        plugin::unsignedValue(instance.config, "timeout_ms", 800));
}

std::string statsText(const UlkStreamStats& stats)
{
    std::ostringstream out;
    out << "status=" << (stats.streaming ? "ready" : "no_data")
        << "\nrunning=" << (stats.running ? 1 : 0)
        << "\nstreaming=" << (stats.streaming ? 1 : 0)
        << "\nlast_sequence=" << stats.lastSequence
        << "\nservice4=" << stats.service4
        << "\nfast120=" << stats.fast120
        << "\nslow200=" << stats.slow200
        << "\nreference204=" << stats.reference204
        << "\nunknown=" << stats.unknown
        << "\ndropped=" << stats.dropped << '\n';
    return out.str();
}

UlkFrame waitYalk(Instance& instance, std::uint64_t after, unsigned timeoutMs)
{
    if (instance.cancelled.load()) throw std::runtime_error("Operation cancelled");
    return instance.transport->waitFrame(
        instance.yalkReference ? UlkFrameKind::Reference204 : UlkFrameKind::Slow200,
        after,
                                         std::chrono::milliseconds(timeoutMs));
}

std::vector<YalkSample> decodeYalk(const Instance& instance,
                                   const std::vector<std::uint8_t>& payload)
{
    return instance.yalkReference
        ? decodeYalkReferenceFrame(payload)
        : decodeYalkSlowFrame(payload);
}

std::string readChannel(Instance& instance, const std::map<std::string, std::string>& args)
{
    const unsigned address = plugin::unsignedValue(args, "ulk_address");
    if (address < 1 || address > 100) throw std::invalid_argument("ulk_address должен быть 1..100");
    const unsigned sampleCount = std::max(1u, plugin::unsignedValue(args, "sample_count", 16));
    std::uint64_t sequence = plugin::unsignedValue(args, "after_sequence",
                                                    static_cast<unsigned>(instance.transport->stats().lastSequence));
    std::uint64_t firstSequence = 0;
    double rawSum = 0.0;
    double codeSum = 0.0;
    unsigned signalOnes = 0;
    for (unsigned sample = 0; sample < sampleCount; ++sample) {
        const auto frame = waitYalk(instance, sequence, timeout(instance, args));
        if (!firstSequence) firstSequence = frame.sequence;
        sequence = frame.sequence;
        const auto words = decodeYalk(instance, frame.payload);
        const auto& value = words[address - 1];
        rawSum += value.rawWord;
        codeSum += value.analogCode;
        if (value.contact) ++signalOnes;
    }
    std::ostringstream out;
    out << std::setprecision(15)
        << "status=ready\nulk_address=" << address
        << "\nraw_mean=" << rawSum / sampleCount
        << "\nanalog_code_mean=" << codeSum / sampleCount
        << "\nsignal=" << (signalOnes * 2 >= sampleCount ? 1 : 0)
        << "\nsignal_ones=" << signalOnes
        << "\nsample_count=" << sampleCount
        << "\nfirst_sequence=" << firstSequence
        << "\nlast_sequence=" << sequence << '\n';
    return out.str();
}

orbita_plugin_status_v1 create(const char*, const char* text, void** output,
                               orbita_plugin_buffer_v1* diagnostic)
{
    return plugin::guarded(diagnostic, [&] {
        if (!output) throw std::invalid_argument("Instance output pointer is required");
        auto instance = std::make_unique<Instance>();
        instance->config = plugin::arguments(text);
        instance->transport = std::make_unique<UlkUdpTransport>(KtmaUlkUdpConfig{
            plugin::required(instance->config, "host"),
            plugin::required(instance->config, "local_address"),
            static_cast<std::uint16_t>(plugin::unsignedValue(instance->config, "data_port", 1113)),
            plugin::unsignedValue(instance->config, "timeout_ms", 800),
            plugin::unsignedValue(instance->config, "queue_capacity", 4096)});
        *output = instance.release();
        return std::string("KTMA ULK UDP adapter created");
    });
}

void destroy(void* value) { delete static_cast<Instance*>(value); }

orbita_plugin_status_v1 invoke(void* value, const char* capability, const char* operation,
                              const char* request, orbita_plugin_buffer_v1* response)
{
    return plugin::guarded(response, [&] {
        auto& instance = *static_cast<Instance*>(value);
        if (!capability || std::string(capability) != "ulk.parameter_source") {
            throw std::invalid_argument("Unsupported capability");
        }
        const auto args = plugin::arguments(request);
        const std::string command = operation ? operation : "";
        if (command == "start_record") {
            std::string path;
            if (args.count("path")) path = args.at("path");
            else {
                const auto root = plugin::required(instance.config, "record_root");
                const auto runId = plugin::required(args, "run_id");
                const auto directory = std::filesystem::path(root) / runId / "ulk";
                std::filesystem::create_directories(directory);
                path = (directory / "frames.ulkbin").string();
            }
            instance.transport->startRecord(path);
            return std::string("status=ok\npath=") + path + '\n';
        }
        if (command == "stop_record") {
            instance.transport->stopRecord();
            return std::string("status=ok\n");
        }
        if(command == "alive")
            {
            const auto before = instance.transport->stats().lastSequence;

            const auto frame = waitYalk(
                instance,
                before,
                timeout(instance,args));

            return std::string("status=ready\n")
                   + "sequnce=" + std::to_string(frame.sequence) + '\n';
        }
        if (command == "prepare_yalk_reference") {
            plugin::requireActiveOutputs(instance.config);
            instance.cancelled.store(false);
            instance.yalkReference = true;
            instance.transport->prepareYalkReference();
            instance.selectedMode = -2;
            return std::string("status=prepared\nprotocol=rokt_yalk\n");
        }
        if (command == "start_prepared_yalk_reference") {
            plugin::requireActiveOutputs(instance.config);
            if (!instance.yalkReference || instance.selectedMode != -2) {
                throw std::runtime_error("ROKT YALK stream was not prepared");
            }
            instance.transport->startPreparedYalkReference();
            const auto frame = waitYalk(instance, 0,
                plugin::unsignedValue(args, "timeout_ms", 3000));
            return std::string("status=ready\nprotocol=rokt_yalk\nmode=reference204\nfirst_sequence=")
                + std::to_string(frame.sequence) + '\n';
        }
        if (command == "start_stream" || command == "probe") {
            plugin::requireActiveOutputs(instance.config);
            instance.cancelled.store(false);
            const std::string protocol = args.count("protocol")
                ? args.at("protocol")
                : (instance.config.count("protocol") ? instance.config.at("protocol") : "legacy_mode6");
            instance.yalkReference = protocol == "rokt_yalk" || protocol == "yalk_kpa";
            if (!instance.yalkReference && protocol != "legacy_mode6") {
                throw std::invalid_argument("Unsupported ULK adapter protocol: " + protocol);
            }
            unsigned mode = 6;
            if (instance.yalkReference) {
                instance.transport->startYalkReference();
                instance.selectedMode = -2;
            } else {
                mode = plugin::unsignedValue(args, "mode", 6);
                if (mode > 255) throw std::invalid_argument("Режим адаптера должен быть 0..255");
                instance.transport->start(static_cast<std::uint8_t>(mode));
                instance.selectedMode = static_cast<int>(mode);
            }
            const auto frame = waitYalk(instance, 0,
                command == "probe" || command == "alive" ? timeout(instance, args)
                                                           : plugin::unsignedValue(args, "timeout_ms", 3000));
            return std::string("status=ready\nprotocol=")
                + (instance.yalkReference ? "rokt_yalk" : "legacy_mode")
                + "\nmode=" + (instance.yalkReference ? "reference204" : std::to_string(mode))
                + "\nfirst_sequence=" + std::to_string(frame.sequence) + '\n';
        }
        if (command == "stop_stream") {
            instance.transport->stop();
            instance.selectedMode = -1;
            instance.yalkReference = false;
            return std::string("status=ok\n");
        }
        if (command == "stats") return statsText(instance.transport->stats());
        if (command == "read_channel" || command == "read") return readChannel(instance, args);
        if (command == "read_snapshot" || command == "read_frame") {
            const std::uint64_t after = plugin::unsignedValue(args, "after_sequence",
                static_cast<unsigned>(instance.transport->stats().lastSequence));
            const auto frame = waitYalk(instance, after, timeout(instance, args));
            const auto words = decodeYalk(instance, frame.payload);
            std::ostringstream out;
            out << "status=ready\nsequence=" << frame.sequence << "\nwords=";
            for (std::size_t index = 0; index < words.size(); ++index) {
                if (index) out << ',';
                out << words[index].rawWord;
            }
            out << '\n';
            return out.str();
        }
        throw std::invalid_argument("Unsupported ULK adapter operation: " + command);
    });
}

void cancel(void* value)
{
    if (!value) return;
    auto& instance = *static_cast<Instance*>(value);
    instance.cancelled.store(true);
    instance.transport->stop();
}

void safeStop(void* value)
{
    if (!value) return;
    auto& instance = *static_cast<Instance*>(value);
    instance.transport->stopRecord();
    instance.transport->stop();
}

const orbita_equipment_api_v1 api{
    ORBITA_EQUIPMENT_ABI_V1, sizeof(orbita_equipment_api_v1),
    "orbita.ktma_adapter_udp", "Адаптер КТМА Ethernet/RS-485", "ulk.parameter_source",
    create, destroy, invoke, cancel, safeStop};
}

extern "C" ORBITA_PLUGIN_EXPORT const orbita_equipment_api_v1* orbita_plugin_get_api_v1(void)
{
    return &api;
}
