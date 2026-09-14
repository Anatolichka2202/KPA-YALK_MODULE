#include "orbita_stand/config.h"
#include "orbita_stand/equipment_runtime.h"
#include "orbita_stand/ulk_udp_transport.h"

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace orbita::stand;

namespace {

std::string hexPrefix(const std::vector<std::uint8_t>& bytes, std::size_t maximum = 32)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    const auto count = std::min(maximum, bytes.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (index) out << ' ';
        out << std::setw(2) << static_cast<unsigned>(bytes[index]);
    }
    return out.str();
}

void printFrames(const std::string& label, const std::vector<UlkFrame>& frames)
{
    std::map<std::size_t, unsigned> sizes;
    for (const auto& frame : frames) ++sizes[frame.payload.size()];

    std::cout << label << " frames=" << frames.size();
    for (const auto& [size, count] : sizes)
        std::cout << " size_" << size << '=' << count;
    std::cout << '\n';

    const auto previewCount = std::min<std::size_t>(frames.size(), 5);
    for (std::size_t index = 0; index < previewCount; ++index) {
        const auto& frame = frames[index];
        std::cout << "  frame[" << index << "] seq=" << frame.sequence
                  << " kind=" << toString(frame.kind)
                  << " bytes=" << frame.payload.size()
                  << " prefix=" << hexPrefix(frame.payload) << '\n';
    }
}

template<typename StartCommand>
std::vector<UlkFrame> captureCommand(UlkUdpTransport& transport,
                                     const std::filesystem::path& recordPath,
                                     unsigned milliseconds,
                                     StartCommand&& startCommand)
{
    // Open the raw recorder before the ROKT command. startYvp*() restarts the
    // receiver/queue but intentionally does not close the record file, so the
    // first UDP frame produced immediately by the command is not lost from raw.
    transport.startRecord(recordPath.string());
    try {
        startCommand();
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        transport.stopRecord();
    } catch (...) {
        transport.stopRecord();
        throw;
    }
    return transport.takeFrames();
}

void writeManifest(const std::filesystem::path& outputDirectory,
                   const std::string& adapterIp,
                   const std::string& localIp,
                   std::uint16_t port,
                   unsigned cell,
                   unsigned captureMs)
{
    std::ofstream manifest(outputDirectory / "capture-manifest.txt",
                           std::ios::out | std::ios::trunc);
    if (!manifest) throw std::runtime_error("Cannot create YVP capture manifest");

    manifest << "schema=1\n"
             << "purpose=yvp_rokt_commissioning\n"
             << "adapter_ip=" << adapterIp << '\n'
             << "local_ip=" << localIp << '\n'
             << "port=" << port << '\n'
             << "cell=" << cell << '\n'
             << "capture_ms=" << captureMs << '\n'
             << "physical_source=Rigol DG-1022Z CH1 -> Z lead of YVP 8-2 cable\n"
             << "N_lead=unused\n"
             << "akip_signal_path=not_used\n"
             << "stimulus_control=manual\n"
             << "stimulus_nominal_v=1\n"
             << "stimulus_amplitude_definition=unconfirmed\n"
             << "payload_decoder=unconfirmed\n";
}

void printResponse(const std::string& stage, const std::string& response)
{
    std::cout << "STAGE " << stage << '\n' << response;
    if (response.empty() || response.back() != '\n') std::cout << '\n';
}

void bindLiveDevices(const StandProfile& profile,
                     EquipmentPluginManager& manager,
                     EquipmentRegistry& registry,
                     std::vector<std::shared_ptr<EquipmentDevice>>& devices,
                     bool activeStimulus)
{
    std::set<std::string> required{
        "ulk.parameter_source", "power.dc_supply"};
    if (activeStimulus) {
        required.insert("stand.switch_matrix");
        required.insert("signal.generator");
    }
    std::set<std::string> bound;

    for (const auto& definition : profile.devices) {
        if (!definition.enabled) continue;
        const bool selected = std::any_of(
            definition.bindCapabilities.begin(), definition.bindCapabilities.end(),
            [&](const std::string& capability) { return required.count(capability) != 0; });
        if (!selected) continue;

        auto config = definition.configuration;
        config["profile.active_outputs_confirmed"] =
            profile.activeOutputsConfirmed ? "true" : "false";
        if (std::find(definition.bindCapabilities.begin(), definition.bindCapabilities.end(),
                      "power.dc_supply") != definition.bindCapabilities.end()) {
            // Live read on the installed AKIP-1160/6 clamps OVP to 28 V.
            // The YVP probe only needs the normal 27 V operating point.
            config["overvoltage_limit_v"] = "28";
        }
        for (const auto& [key, value] : profile.routes) config["route." + key] = value;
        auto device = manager.createDevice(definition.pluginId, definition.id, config);
        for (const auto& capability : definition.bindCapabilities) {
            if (required.count(capability)) {
                registry.bind(capability, device);
                bound.insert(capability);
            }
        }
        devices.push_back(std::move(device));
    }

    for (const auto& capability : required) {
        if (!bound.count(capability))
            throw std::runtime_error("Live YVP probe requires capability: " + capability);
    }
}

int runLiveProbe(int argc, char** argv)
{
    if (argc < 4 || argc > 8) {
        std::cerr << "Usage: orbita_yvp_rokt_probe --live <stand-profile.yaml> "
                     "<plugin-directory> [output-directory] [cell] [capture-ms] "
                     "[--stimulus]\n";
        return 1;
    }

    const std::filesystem::path profilePath = argv[2];
    const std::filesystem::path pluginDirectory = argv[3];
    const std::filesystem::path outputDirectory = argc >= 5
        ? std::filesystem::path(argv[4]) : std::filesystem::path("yvp-live-capture");
    const unsigned cell = argc >= 6 ? static_cast<unsigned>(std::stoul(argv[5])) : 1;
    const unsigned captureMs = argc >= 7 ? static_cast<unsigned>(std::stoul(argv[6])) : 1500;
    const bool activeStimulus = argc >= 8 && std::string(argv[7]) == "--stimulus";
    if (argc >= 8 && !activeStimulus)
        throw std::invalid_argument("Expected --stimulus as the final argument");
    if (cell < 1 || cell > 255) throw std::invalid_argument("cell must be 1..255");
    if (!captureMs) throw std::invalid_argument("capture_ms must be > 0");

    std::filesystem::create_directories(outputDirectory);
    const auto capturePath = std::filesystem::absolute(outputDirectory / "yvp-live.ulkbin");

    const auto profile = loadStandProfile(profilePath.string());
    if (!profile.activeOutputsConfirmed)
        throw std::runtime_error("Active outputs are blocked by the stand profile");

    EquipmentPluginManager manager;
    manager.loadDirectory(pluginDirectory.string());
    EquipmentRegistry registry;
    std::vector<std::shared_ptr<EquipmentDevice>> devices;
    bindLiveDevices(profile, manager, registry, devices, activeStimulus);

    std::cout << "YVP LIVE ADAPTER PROBE\n"
              << "profile=" << profile.id << ' ' << profile.version << '\n'
              << "capture=" << capturePath.string() << '\n'
              << (activeStimulus
                      ? "stimulus=ISD input 1/contact 33; K=1/contact 2; Rigol CH1 20 Hz, 2 Vpp\n"
                      : "stimulus=not applied; adapter transport only\n")
              << "supply=27 V; current limit=0.6 A; OVP=28 V (live AKIP limit)\n"
              << "decoder=disabled; verdict=not produced; V7=not used\n";

    bool recording = false;
    auto invoke = [&](const std::string& capability, const std::string& operation,
                      std::map<std::string, std::string> arguments = {}) {
        const auto response = registry.invoke(capability, operation, arguments);
        printResponse(capability + "." + operation, response);
        return response;
    };
    auto cleanup = [&] {
        if (recording) {
            try { registry.invoke("ulk.parameter_source", "stop_record", {}); } catch (...) {}
            recording = false;
        }
        try { registry.invoke("ulk.parameter_source", "stop_stream", {}); } catch (...) {}
        if (activeStimulus) {
            try { registry.invoke("signal.generator", "output",
                                  {{"channel", "1"}, {"enabled", "false"}}); } catch (...) {}
            try { registry.invoke("stand.switch_matrix", "full_reset", {}); } catch (...) {}
        }
        try { registry.invoke("power.dc_supply", "output", {{"enabled", "false"}}); } catch (...) {}
        registry.safeStopAll();
    };

    try {
        invoke("power.dc_supply", "set_voltage", {{"volts", "27"}});
        invoke("power.dc_supply", "set_current_limit", {{"amperes", "0.6"}});
        invoke("power.dc_supply", "output", {{"enabled", "true"}});
        std::cout << "WAIT adapter_boot_ms=8000\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(8000));
        invoke("power.dc_supply", "read_state");

        invoke("ulk.parameter_source", "start_record", {{"path", capturePath.string()}});
        recording = true;

        // The archived KPA sequence enters the ordinary YALK reference mode
        // before switching the same adapter to YVP1k.  Failure here is printed,
        // but does not suppress the two YVP commands that are under test.
        try {
            invoke("ulk.parameter_source", "prepare_yalk_reference");
            invoke("ulk.parameter_source", "start_prepared_yalk_reference", {{"timeout_ms", "3000"}});
            invoke("ulk.parameter_source", "stats");
        } catch (const std::exception& error) {
            std::cout << "YALK_PREPARE_FAILED " << error.what() << '\n';
        }

        if (activeStimulus) {
            invoke("stand.switch_matrix", "full_reset");
            std::cout << "WAIT isd_reset_settle_ms=400\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            invoke("stand.switch_matrix", "switch",
                   {{"type", "2"}, {"channel", "33"}, {"enabled", "true"}});
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            invoke("stand.switch_matrix", "switch",
                   {{"type", "2"}, {"channel", "2"}, {"enabled", "true"}});
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            invoke("signal.generator", "set_sine",
                   {{"channel", "1"}, {"frequency_hz", "20"},
                    {"amplitude_vpp", "2"}, {"offset_v", "0"}});
            invoke("signal.generator", "output",
                   {{"channel", "1"}, {"enabled", "false"}});
        }

        invoke("ulk.parameter_source", "start_yvp_probe",
               {{"cell", std::to_string(cell)}, {"readout_yalk", "false"}});
        std::this_thread::sleep_for(std::chrono::milliseconds(captureMs));
        invoke("ulk.parameter_source", "stats");
        invoke("ulk.parameter_source", "read_yvp_raw",
               {{"frame_count", "32"}, {"timeout_ms", "3000"}});

        if (activeStimulus) {
            invoke("signal.generator", "output",
                   {{"channel", "1"}, {"enabled", "true"}});
            std::cout << "WAIT stimulus_settle_ms=500\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            invoke("ulk.parameter_source", "start_yvp_probe",
                   {{"cell", std::to_string(cell)}, {"readout_yalk", "false"}});
            std::this_thread::sleep_for(std::chrono::milliseconds(captureMs));
            invoke("ulk.parameter_source", "stats");
            invoke("ulk.parameter_source", "read_yvp_raw",
                   {{"frame_count", "32"}, {"timeout_ms", "3000"}});
        }

        for (unsigned channel = 1; channel <= 8; ++channel) {
            invoke("ulk.parameter_source", "start_yvp_channel_probe",
                   {{"channel", std::to_string(channel)},
                    {"cell", std::to_string(cell)}, {"readout_yalk", "false"}});
            std::this_thread::sleep_for(std::chrono::milliseconds(captureMs));
            invoke("ulk.parameter_source", "stats");
            invoke("ulk.parameter_source", "read_yvp_channel_raw",
                   {{"channel", std::to_string(channel)}, {"timeout_ms", "1000"}});

            // 0A 03 is the channel-selection exchange.  Re-enter 0A 01 and
            // inspect its 128-byte measurement stream after the selection.
            invoke("ulk.parameter_source", "start_yvp_probe",
                   {{"cell", std::to_string(cell)}, {"readout_yalk", "false"}});
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            invoke("ulk.parameter_source", "read_yvp_raw",
                   {{"frame_count", "16"}, {"timeout_ms", "2000"}});
        }

        cleanup();
        std::cout << "RESULT capture_complete; no decoder and no acceptance verdict\n";
        return 0;
    } catch (...) {
        cleanup();
        throw;
    }
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try {
        if (argc >= 2 && std::string(argv[1]) == "--live")
            return runLiveProbe(argc, argv);

        const std::string adapterIp = argc >= 2 ? argv[1] : "192.168.0.115";
        const std::string localIp = argc >= 3 ? argv[2] : "192.168.0.50";
        const std::uint16_t port = argc >= 4
            ? static_cast<std::uint16_t>(std::stoul(argv[3])) : 1113;
        const unsigned cell = argc >= 5 ? static_cast<unsigned>(std::stoul(argv[4])) : 1;
        const unsigned captureMs = argc >= 6 ? static_cast<unsigned>(std::stoul(argv[5])) : 1000;
        const std::filesystem::path outputDirectory = argc >= 7
            ? std::filesystem::path(argv[6]) : std::filesystem::path("yvp-capture");

        if (cell < 1 || cell > 255)
            throw std::invalid_argument("cell must be 1..255");
        if (!captureMs)
            throw std::invalid_argument("capture_ms must be > 0");

        std::filesystem::create_directories(outputDirectory);
        writeManifest(outputDirectory, adapterIp, localIp, port, cell, captureMs);

        UlkUdpTransport transport(KtmaUlkUdpConfig{
            adapterIp, localIp, port, 800, 4096});

        std::cout << "YVP ROKT raw probe\n"
                  << "adapter=" << adapterIp << ':' << port
                  << " local=" << localIp
                  << " cell=" << cell
                  << " capture_ms=" << captureMs << '\n'
                  << "SETUP: Rigol DG-1022Z CH1 -> Z lead; N lead unused; AKIP is not in the YVP signal path.\n"
                  << "STIMULUS: set 1 V manually; amplitude definition is intentionally not assumed by software.\n"
                  << "NOTE: this tool does not decode YVP payload and does not control the generator or ISD.\n";

        auto frames = captureCommand(
            transport, outputDirectory / "yvp-mode.ulkbin", captureMs,
            [&] { transport.startYvpRokt(static_cast<std::uint8_t>(cell)); });
        printFrames("ROKT_0A_01", frames);

        for (unsigned channel = 1; channel <= 8; ++channel) {
            const auto path = outputDirectory /
                ("yvp-channel-" + std::to_string(channel) + ".ulkbin");
            frames = captureCommand(transport, path, captureMs, [&] {
                transport.startYvpChannelRokt(
                    static_cast<std::uint8_t>(channel), static_cast<std::uint8_t>(cell));
            });
            printFrames("ROKT_0A_03 channel=" + std::to_string(channel), frames);
        }

        transport.stop();
        std::cout << "RESULT raw captures and manifest written to "
                  << outputDirectory.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << '\n';
        return 1;
    }
}
