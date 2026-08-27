#include "orbita.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string normalizeAddress(std::string line)
{
    const auto end = line.find_first_of(" \t\r\n");
    if (end != std::string::npos) line.resize(end);
    const std::map<unsigned char, char> cp1251ToLatin = {
        {0xC0, 'A'}, {0xC2, 'B'}, {0xC5, 'E'}, {0xCA, 'K'},
        {0xCC, 'M'}, {0xCD, 'H'}, {0xCE, 'O'}, {0xCF, 'P'},
        {0xD0, 'P'}, {0xD1, 'C'}, {0xD2, 'T'}, {0xD5, 'X'}};
    for (char& character : line) {
        const auto replacement = cp1251ToLatin.find(static_cast<unsigned char>(character));
        if (replacement != cp1251ToLatin.end()) character = replacement->second;
        else character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return line;
}

std::vector<orbita::ChannelSpec> loadChannels(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(std::string("Cannot open address file: ") + path);

    std::vector<orbita::ChannelSpec> result;
    for (std::string line; std::getline(input, line);) {
        const auto address = normalizeAddress(std::move(line));
        if (!address.empty()) result.push_back({address, {}, {}});
    }
    if (result.empty()) throw std::runtime_error("Address file contains no channels");
    return result;
}

void printSnapshot(const orbita::Snapshot& snapshot)
{
    std::size_t valid = 0;
    for (const auto& value : snapshot.values) if (value.valid) ++valid;
    std::cout << "SNAPSHOT mtv=" << snapshot.mtv_seconds
              << " frames=" << snapshot.stats.frames_processed
              << " phrase_error_percent=" << snapshot.stats.phrase_error_percent
              << " group_error_percent=" << snapshot.stats.group_error_percent
              << " mb_per_second=" << snapshot.stats.mb_per_second
              << " valid=" << valid << '/' << snapshot.values.size() << '\n';
    for (const auto& value : snapshot.values) {
        if (value.valid) std::cout << "VALUE " << value.address << '=' << value.value << '\n';
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: orbita_telemetry_probe <address-file> [seconds] [interval-ms]\n";
        return 2;
    }
    const int seconds = argc >= 3 ? std::max(1, std::atoi(argv[2])) : 10;
    const int intervalMilliseconds = argc == 4
        ? std::max(100, std::atoi(argv[3]))
        : 1000;
    try {
        orbita::Orbita source;
        source.setDeviceE2010(0, 10000.0);
        const auto channels = loadChannels(argv[1]);
        source.setChannels(channels);
        std::cout << "CHANNELS " << channels.size() << '\n';
        source.start();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        auto nextSnapshot = std::chrono::steady_clock::now();
        bool received = false;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto now = std::chrono::steady_clock::now();
            if (now < nextSnapshot) {
                std::this_thread::sleep_until(std::min(nextSnapshot, deadline));
                continue;
            }
            if (source.waitForData(std::chrono::milliseconds(intervalMilliseconds))) {
                printSnapshot(source.getSnapshot());
                received = true;
                nextSnapshot = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(intervalMilliseconds);
            }
        }
        source.stop();
        return received ? 0 : 3;
    } catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << '\n';
        return 1;
    }
}
