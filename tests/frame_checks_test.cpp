#include "frame_checks.h"
#include "miltech/orbita_sample_bridge.h"
#include "miltech/orbita_sample_simulator.h"
#include "orbita_stand/sample_source.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

using namespace miltech::brp;
using namespace std::chrono_literals;

namespace {
void require(bool result, const char* message)
{
    if (!result) throw std::runtime_error(message);
}

std::vector<uint16_t> groupWordsAt(std::size_t index)
{
    std::vector<uint16_t> words(groupWords);
    for (std::size_t word = 0; word < words.size(); ++word)
        words[word] = simulatedWord(index * groupWords + word);
    return words;
}
}

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "missing checks config");
        std::ifstream file(argv[1], std::ios::binary);
        require(file.good(), "cannot open checks config");
        const std::string yaml{std::istreambuf_iterator<char>(file), {}};
        const auto checks = loadFrameChecks(yaml);
        require(checks.size() == 12, "unexpected table coverage");
        require(checks.front().layouts.size() == 4, "missing POS channels");
        require(checks.front().layouts.front().firstWord == 256, "wrong address index");
        require(checks.front().layouts.front().stride == 1024, "wrong address stride");

        unsigned next = 1023;
        const auto bytes = counterBytes(next, 2);
        require(bytes == std::vector<uint8_t>{0x5F, 0x9F, 0x40, 0x80} && next == 1,
                "wrong COM layout at wrap");

        CounterCheck counter;
        for (int group = 1; group <= 4; ++group)
            counter.consume(groupWordsAt(group - 1), group, 1);
        require(!counter.passed(), "two complete periods and their final wrap are required");
        counter.consume(groupWordsAt(4), 5, 1);
        require(counter.passed() && counter.trace().size() == 2049, "good ramp rejected");
        require(counter.trace().at(1023) == 1023 && counter.trace().at(1024) == 0,
                "graph must show the wrap");

        CounterCheck interrupted;
        interrupted.consume(groupWordsAt(0), 1, 1);
        interrupted.consume(groupWordsAt(1), 3, 1);
        require(interrupted.contiguous() == 512, "missing group preserved a stale sequence");
        auto corrupt = groupWordsAt(2);
        corrupt[1001] ^= 0x08;
        interrupted.consume(corrupt, 4, 1);
        require(interrupted.contiguous() < 512, "wrong byte preserved a stale sequence");

        CodeRange codes;
        require(!codes.matches(0, 1023), "missing samples cannot pass");
        codes.consume(groupWordsAt(0), checks.front(), 0);
        require(codes.count == 2 && codes.matches(512, 512) && !codes.matches(0, 511),
                "wrong code range or count");

        // Сквозной прогон использует штатный тракт отсчёты → декодер M16.
        // Никакие COM-порты, источники питания и ИСД здесь не открываются.
        auto source = miltech::integration::createOrbitaSampleSimulator(simulatedWord);
        require(source->open(), "could not open simulator");
        CounterCheck decoded;
        CodeRange decodedCodes;
        std::atomic_bool completed{false};
        std::atomic_uint groups{0};
        unsigned measuredGroups = 0;
        orbita::Orbita decoder;
        decoder.setGroupCallback([&](const auto& words, int group, int cycle) {
            if (!decoded.passed()) {
                decoded.consume(words, group, cycle);
            } else if (measuredGroups < 128) {
                decodedCodes.consume(words, checks.front(), 0);
                completed = ++measuredGroups == 128;
            }
            ++groups;
        });
        miltech::integration::OrbitaSampleBridge bridge(*source, decoder);
        if (!bridge.start()) throw std::runtime_error(bridge.lastError());
        const auto deadline = std::chrono::steady_clock::now() + 8s;
        while (!completed && bridge.lastError().empty() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(10ms);
        bridge.stop();
        if (!completed) {
            std::cerr << "decoded groups=" << groups << " contiguous=" << decoded.contiguous()
                      << " measured groups=" << measuredGroups
                      << " bridge error=" << bridge.lastError() << '\n';
        }
        require(decoded.passed(), "synthetic M16 did not reproduce the counter");
        require(completed && decodedCodes.count == 256 && decodedCodes.matches(512, 512),
                "addressed M16 samples changed after the interface gate");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
