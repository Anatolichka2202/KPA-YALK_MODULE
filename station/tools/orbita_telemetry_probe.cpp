#include "orbita.h"
#include "orbita_stand/component_runtime.h"
#include "orbita_stand/config.h"
#include "orbita_stand/sample_source.h"
#include "miltech/orbita_sample_bridge.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kOrbitaSampleSourceBinding = "telemetry.orbita.sample_source";

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

std::size_t printSnapshot(const char* kind, const orbita::Snapshot& snapshot)
{
    std::size_t valid = 0;
    for (const auto& value : snapshot.values) if (value.valid) ++valid;
    std::cout << kind << " mtv=" << snapshot.mtv_seconds
              << " frames=" << snapshot.stats.frames_processed
              << " phrase_error_percent=" << snapshot.stats.phrase_error_percent
              << " group_error_percent=" << snapshot.stats.group_error_percent
              << " mb_per_second=" << snapshot.stats.mb_per_second
              << " valid=" << valid << '/' << snapshot.values.size() << '\n';
    for (const auto& value : snapshot.values) {
        if (value.valid) std::cout << "VALUE " << value.address << '=' << value.value << '\n';
    }
    return valid;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--validate") {
        try {
            const auto channels = loadChannels(argv[2]);
            std::cout << "VALID channels=" << channels.size() << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "ERROR " << error.what() << '\n';
            return 1;
        }
    }

    if (argc < 3 || argc > 5) {
        std::cerr
            << "Usage: orbita_telemetry_probe <stand-profile> <address-file> [seconds] [interval-ms]\n"
            << "   or: orbita_telemetry_probe --validate <address-file>\n";
        return 2;
    }

    const int seconds = argc >= 4 ? std::max(1, std::atoi(argv[3])) : 10;
    const int intervalMilliseconds = argc >= 5
        ? std::max(100, std::atoi(argv[4]))
        : 1000;

    try {
        const auto profile = orbita::stand::loadStandProfile(argv[1]);
        orbita::stand::ComponentRuntime components;
        orbita::stand::registerSampleSourceComponents(components);
        components.instantiate(profile, {"sample_source"});

        auto* sampleSource = components.findAs<orbita::stand::ISampleSource>(
            kOrbitaSampleSourceBinding);
        if (!sampleSource) {
            throw std::runtime_error(
                std::string("Stand profile has no sample source bound to ")
                + kOrbitaSampleSourceBinding);
        }

        orbita::Orbita decoder;
        const auto channels = loadChannels(argv[2]);
        decoder.setChannels(channels);

        miltech::integration::OrbitaSampleBridge bridge(*sampleSource, decoder);
        bridge.setErrorCallback([](const std::string& message) {
            std::cerr << "SOURCE_ERROR " << message << '\n';
        });

        std::cout << "CHANNELS " << channels.size()
                  << " profile=" << profile.id
                  << " profile_version=" << profile.version
                  << " source_binding=" << kOrbitaSampleSourceBinding << '\n';

        if (!bridge.start()) {
            throw std::runtime_error(
                bridge.lastError().empty()
                    ? "Station sample source / Orbita bridge failed to start"
                    : bridge.lastError());
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        auto nextSnapshot = std::chrono::steady_clock::now();
        bool received = false;
        std::size_t lastValid = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto now = std::chrono::steady_clock::now();
            if (now < nextSnapshot) {
                std::this_thread::sleep_until(std::min(nextSnapshot, deadline));
                continue;
            }
            if (decoder.waitForData(std::chrono::milliseconds(intervalMilliseconds))) {
                lastValid = printSnapshot("SNAPSHOT", decoder.getSnapshot());
                received = true;
                nextSnapshot = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(intervalMilliseconds);
            }
        }

        const auto finalSnapshot = decoder.getSnapshot();
        bridge.stop();
        lastValid = printSnapshot("FINAL", finalSnapshot);
        std::cout << "RESULT received=" << (received ? "true" : "false")
                  << " valid=" << lastValid << '/' << channels.size()
                  << " frames=" << finalSnapshot.stats.frames_processed << '\n';
        return received ? 0 : 3;
    } catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << '\n';
        return 1;
    }
}
