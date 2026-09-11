#include "orbita_stand/ulk_udp_transport.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
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

std::vector<UlkFrame> capture(UlkUdpTransport& transport,
                              const std::filesystem::path& recordPath,
                              unsigned milliseconds)
{
    transport.startRecord(recordPath.string());
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    transport.stopRecord();
    return transport.takeFrames();
}

} // namespace

int main(int argc, char** argv)
{
    try {
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
        UlkUdpTransport transport(KtmaUlkUdpConfig{
            adapterIp, localIp, port, 800, 4096});

        std::cout << "YVP ROKT raw probe\n"
                  << "adapter=" << adapterIp << ':' << port
                  << " local=" << localIp
                  << " cell=" << cell
                  << " capture_ms=" << captureMs << '\n'
                  << "NOTE: this tool does not decode YVP payload and does not control the generator or ISD.\n";

        transport.startYvpRokt(static_cast<std::uint8_t>(cell));
        auto frames = capture(transport, outputDirectory / "yvp-mode.ulkbin", captureMs);
        printFrames("ROKT_0A_01", frames);

        for (unsigned channel = 1; channel <= 8; ++channel) {
            transport.startYvpChannelRokt(
                static_cast<std::uint8_t>(channel), static_cast<std::uint8_t>(cell));
            const auto path = outputDirectory /
                ("yvp-channel-" + std::to_string(channel) + ".ulkbin");
            frames = capture(transport, path, captureMs);
            printFrames("ROKT_0A_03 channel=" + std::to_string(channel), frames);
        }

        transport.stop();
        std::cout << "RESULT raw captures written to " << outputDirectory.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << '\n';
        return 1;
    }
}
