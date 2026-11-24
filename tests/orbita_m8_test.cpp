#include "miltech/orbita_sample_simulator.h"
#include "orbita.h"
#include "orbita_stand/sample_source.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;

void require(bool result, const char* message)
{
    if (!result) throw std::runtime_error(message);
}

unsigned sampleCode(std::size_t word)
{
    return (word / 1024 * 13 + word % 1024) & 1023;
}

struct GroupObservation {
    int group;
    int cycle;
    std::size_t size;
    uint16_t at897;
    std::optional<double> channel;
    std::optional<double> slow;
};

void testM8()
{
    const std::string address = "M08A10B10C12D80T01P01";
    const std::string slowAddress = "M08A10B10C10D10E20T01";
    const auto layout = orbita::m8GroupChannel(address);
    require(layout && layout->firstWord == 896 && layout->stride == 1024,
            "appendix M8 service address must begin at word 897");
    require(!orbita::m8GroupChannel(slowAddress), "slow address must not repeat every group");

    orbita::Orbita decoder(orbita::Informativity::M8);
    decoder.setChannels({{address, {}, {}}, {slowAddress, {}, {}},
                         {"M16P1A10T01", {}, {}}, {"M08A10B10C10D10E10X20T01", {}, {}}});
    require(decoder.getChannels().size() == 2, "M8 accepted a mismatched or frame-relative address");
    decoder.setInvertSignal(true);

    std::vector<GroupObservation> received;
    std::atomic<unsigned> groupCount{0};
    std::atomic_bool skippedSamples{false};
    decoder.setGroupCallback([&](const auto& words, int group, int cycle) {
        if (words.size() > 896) {
            received.push_back({group, cycle, words.size(), words[896],
                                decoder.getValueByAddress(address), decoder.getValueByAddress(slowAddress)});
        }
        groupCount = static_cast<unsigned>(received.size());
    });

    QTemporaryDir recordings;
    require(recordings.isValid(), "temporary directory unavailable");
    const auto path = recordings.filePath("m8.tlm").toUtf8().toStdString();
    auto source = miltech::integration::createOrbitaM8SampleSimulator([](std::size_t word) {
        return static_cast<uint16_t>(sampleCode(word) << 1);
    });
    require(source->open(), "M8 simulator did not open");

    // Подаём инвертированный сигнал и намеренно выбираем размер порции,
    // который не совпадает ни с парой фраз, ни с половиной бита.
    source->setSamplesCallback([&](const std::vector<int16_t>& samples) {
        // После первого полного цикла имитируем разрыв с потерей части
        // фразы. Поток после разрыва должен синхронизироваться на МЦ.
        const bool interrupt = groupCount >= 34 && samples.size() > 137
            && !skippedSamples.exchange(true);
        for (std::size_t offset = interrupt ? 137 : 0; offset < samples.size();) {
            const auto end = std::min(samples.size(), offset + (offset ? 4093 : 47));
            std::vector<int16_t> part(samples.begin() + offset, samples.begin() + end);
            for (auto& sample : part) sample = -sample;
            decoder.pushSamples(part);
            offset = end;
        }
    });
    decoder.start();
    decoder.startRecording(path);
    require(source->start(), "M8 simulator did not start");
    const auto deadline = std::chrono::steady_clock::now() + 8s;
    while (groupCount < 44 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(10ms);
    source->stop();
    decoder.stopRecording();
    decoder.stop();

    require(received.size() >= 44, "did not resume decoding after the M8 signal gap");
    const auto& first = received.front();
    require(first.group == 1 && first.cycle == 1 && first.size == 1024, "first M8 group not synchronized");
    const auto expectedCode = sampleCode(32 * 1024 + 896);
    require(first.at897 == (expectedCode << 1) && first.channel == expectedCode,
            "wrong indexed M8 word or channel code");
    require(!received[3].slow && received[4].slow == sampleCode(36 * 1024)
        && received[5].slow == received[4].slow, "slow M8 address updated outside group 5");
    require(received[31].group == 32 && received[31].cycle == 1
        && received[32].group == 1 && received[32].cycle == 2, "M8 cycle did not advance");
    const auto resumed = std::find_if(received.begin() + 34, received.end(),
        [](const auto& observation) { return observation.group == 1 && observation.cycle == 1; });
    const auto stats = decoder.getStats();
    require(skippedSamples && resumed != received.end() && stats.group_error_percent > 0,
            "M8 group sync did not restart at a cycle after loss");
    require(stats.frames_processed == received.size(), "M8 group count is wrong");

    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    std::string header(256, '\0');
    file.read(header.data(), header.size());
    require(header.find("INF=8.") != std::string::npos, "wrong format in TLM header");
    char prefix[32]{};
    file.read(prefix, sizeof(prefix));
    require(file.good() && static_cast<unsigned char>(prefix[0]) == 1
        && static_cast<unsigned char>(prefix[4]) == 0
        && static_cast<unsigned char>(prefix[5]) == 128, "M8 TLM block needs 32768 words");
    uint16_t firstWord = 0;
    file.read(reinterpret_cast<char*>(&firstWord), sizeof(firstWord));
    require(firstWord == (sampleCode(32 * 1024) << 1), "incorrect M8 TLM word after the 32-byte prefix");
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        testM8();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
