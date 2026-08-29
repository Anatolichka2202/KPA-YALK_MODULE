#include "orbita_stand/ulk_udp_transport.h"
#include "orbita_stand/yalk_frame.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

unsigned number(const char* text, const char* name, unsigned maximum)
{
    std::size_t parsed = 0;
    const unsigned long value = std::stoul(text, &parsed, 0);
    if (parsed != std::string(text).size() || value > maximum) {
        throw std::invalid_argument(std::string("Invalid ") + name);
    }
    return static_cast<unsigned>(value);
}

void printPacket(unsigned index, const std::vector<std::uint8_t>& bytes)
{
    std::cout << "PACKET index=" << index << " size=" << bytes.size() << " hex=";
    const std::size_t preview = std::min<std::size_t>(bytes.size(), 64);
    for (std::size_t offset = 0; offset < preview; ++offset) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned>(bytes[offset]);
    }
    std::cout << std::dec << '\n';

    if (bytes.size() != 200 && bytes.size() != 204) return;
    const auto words = orbita::stand::decodeYalkSlowFrame(bytes);
    unsigned nonzero = 0;
    for (const auto& word : words) if (word.rawWord) ++nonzero;
    std::cout << "YALK_WORDS count=" << words.size() << " nonzero=" << nonzero << '\n';
    for (std::size_t word = 0; word < words.size(); ++word) {
        std::cout << "WORD index=" << word
                  << " raw=" << words[word].rawWord
                  << " analog=" << words[word].analogCode
                  << " signal=" << words[word].contact << '\n';
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 6 && argc != 7) {
        std::cerr << "Usage: orbita_ubsi_udp_probe <adapter-ip> <local-ip> "
                     "<data-port> <ack-port> <seconds> [mode]\n"
                     "Without [mode] the probe is passive. Mode 6 is YALK/ULK "
                     "for the archived KTMA firmware.\n";
        return 2;
    }

    try {
        const auto dataPort = static_cast<std::uint16_t>(number(argv[3], "data port", 65535));
        const auto ackPort = static_cast<std::uint16_t>(number(argv[4], "ack port", 65535));
        const unsigned seconds = std::max(1u, number(argv[5], "duration", 3600));
        (void)ackPort;
        orbita::stand::UlkUdpTransport adapter({argv[1], argv[2], dataPort, 800, 4096});

        std::cout << "TARGET adapter=" << argv[1] << " local=" << argv[2]
                  << " data_port=" << dataPort << " ack_port=" << ackPort << '\n';
        if (argc == 7) {
            const auto mode = static_cast<std::uint8_t>(number(argv[6], "mode", 255));
            std::cout << "ACTIVE_SELECT mode=" << static_cast<unsigned>(mode) << '\n';
            adapter.start(mode);
            std::cout << "STREAM mode=" << static_cast<unsigned>(mode) << '\n';
        } else {
            adapter.start(6);
            std::cout << "ACTIVE_SELECT mode=6\n";
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        unsigned packets = 0;
        unsigned timeouts = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            try {
                const auto after = adapter.stats().lastSequence;
                const auto frame = adapter.waitFrame(orbita::stand::UlkFrameKind::Slow200, after,
                    std::chrono::milliseconds(800));
                printPacket(++packets, frame.payload);
            } catch (const std::runtime_error& error) {
                ++timeouts;
                std::cout << "WAIT " << error.what() << '\n';
            }
        }
        std::cout << "RESULT packets=" << packets << " timeouts=" << timeouts << '\n';
        adapter.stop();
        return packets ? 0 : 3;
    } catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << '\n';
        return 1;
    }
}
