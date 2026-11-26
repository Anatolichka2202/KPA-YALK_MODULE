#include "frame_checks.h"
#include "miltech/orbita_sample_bridge.h"
#include "miltech/orbita_sample_simulator.h"
#include "orbita_stand/sample_source.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
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
        require(checks.front().layouts.front().firstWord == 16, "wrong 2026 address index");
        require(checks.front().layouts.front().stride == 128, "wrong 2026 address stride");
        const auto byNumber = [&](int number) -> const FrameCheck& {
            const auto found = std::find_if(checks.begin(), checks.end(),
                [number](const FrameCheck& check) { return check.number == number; });
            require(found != checks.end(), "required row is missing");
            return *found;
        };

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
        require(counter.recent().size() == 512 && counter.latestValue()
                && *counter.latestValue() == 0 && counter.decodedValues() == 2049,
                "live counter must preserve only the latest 512 codes");
        counter.consume(groupWordsAt(5), 6, 1);
        require(counter.passed() && counter.trace().size() == 2049
                && counter.recent().size() == 512 && counter.decodedValues() > 2049,
                "confirmed TU result must stay valid while live counter continues");

        CounterCheck interrupted;
        interrupted.consume(groupWordsAt(0), 1, 1);
        interrupted.consume(groupWordsAt(1), 3, 1);
        require(interrupted.contiguous() == 512, "missing group preserved a stale sequence");
        auto corrupt = groupWordsAt(2);
        corrupt[1001] ^= 0x08;
        interrupted.consume(corrupt, 4, 1);
        require(interrupted.contiguous() < 512, "wrong byte preserved a stale sequence");
        require(!interrupted.recent().empty() && interrupted.decodedValues() > 512
                && interrupted.breaks() > 0,
                "live graph discarded all samples when sequence continuity was lost");
        CounterCheck noReply;
        noReply.consume(std::vector<uint16_t>(groupWords, 0), 1, 1);
        require(noReply.groupsReceived() == 1 && noReply.markedWords() == 0
                && noReply.decodedValues() == 0 && noReply.recent().empty(),
                "diagnostic must distinguish M16 groups from actual RS485 payload");

        CodeRange codes;
        require(!codes.matches(0, 1023), "missing samples cannot pass");
        codes.consume(groupWordsAt(0), checks.front(), 0);
        require(codes.count == 16 && codes.matches(512, 512) && !codes.matches(0, 511),
                "wrong code range or count");

        const auto point = compareVoltage(*byNumber(3).scale, codes, 20.10, 20.10);
        require(point.toleranceVolts && std::abs(*point.toleranceVolts - 0.4) < 1e-9,
                "1 percent of 40 V must be 0.4 V");
        require(std::abs(point.maximumErrorVolts - (20.10 - 40.0 * 512 / 1023)) < 1e-9,
                "V7 comparison uses the wrong conversion");
        const auto drift = compareVoltage(*byNumber(3).scale, codes, 20.02, 20.51);
        require(drift.maximumErrorVolts > *drift.toleranceVolts,
                "V7 drift must affect the comparison");
        require(byNumber(1).scale->minVolts == -6.0 && byNumber(1).scale->maxVolts == 6.0
                && byNumber(1).scale->tolerancePercent
                && byNumber(1).pointTargets == std::vector<double>{-5.0, 0.0, 5.0}
                && std::abs(byNumber(1).targetWindowVolts - 0.5) < 1e-9,
                "POS four channels must use the configured multi-point ±6 V sweep");

        auto discrete = groupWordsAt(0);
        const auto& contacts = byNumber(10);
        const auto layout = contacts.layouts[0];
        require(contacts.shifts == std::vector<unsigned>{3, 4, 5, 6}, "wrong dry contact bit order");
        for (auto i = layout.firstWord; i < discrete.size(); i += layout.stride)
            discrete[i] = (1u << 3) | (1u << 5) | (1u << 7);
        for (unsigned channel = 0; channel < 4; ++channel) {
            CodeRange state;
            state.consume(discrete, contacts, channel);
            require(state.matches(channel % 2 == 0, channel % 2 == 0), "wrong dry contact value");
        }
        CodeRange command;
        command.consume(discrete, byNumber(6), 0);
        require(command.matches(1, 1), "wrong Command 1 bit");
        CodeRange fill;
        fill.consume(std::vector<uint16_t>(groupWords, 0x2AA), byNumber(2), 0);
        require(fill.count && fill.count == fill.fillCount, "missing FPGA fill detection");

        YasmCheck absent({0x01, 0x02, 0x03});
        absent.consume(groupWordsAt(0), 1, 1);
        require(!absent.passed(), "YASM accepted unrelated stream");
        auto busFixture = YasmCheck::fromBusWords({0x1234, 0x45AB});
        std::vector<uint16_t> yasmSlots(groupWords, 0x2AA);
        // Вставляем командный слот между двумя словами данных контроллера.
        const std::vector<unsigned> yasmCodes{0x34 << 2, (0x12 << 2) | 2,
                                               (0x20 << 2) | 1,
                                               0xAB << 2, (0x45 << 2) | 2};
        for (std::size_t i = 0; i < yasmCodes.size(); ++i)
            yasmSlots[2 + 4 * i] = static_cast<uint16_t>(yasmCodes[i] << 1);
        busFixture.consume(yasmSlots, 1, 1);
        require(busFixture.passed(), "BC fixture did not match YASM payload");
        auto corruptBus = YasmCheck::fromBusWords({0x1234, 0x45AB});
        yasmSlots[14] ^= 1 << 3;
        corruptBus.consume(yasmSlots, 1, 1);
        require(!corruptBus.passed(), "BC fixture accepted a corrupted payload");

        // Сквозной прогон использует штатный тракт отсчёты → декодер M16.
        // Никакие COM-порты, источники питания и ИСД здесь не открываются.
        auto source = miltech::integration::createOrbitaSampleSimulator(simulatedWord);
        require(source->open(), "could not open simulator");
        CounterCheck decoded;
        YasmCheck decodedYasm({simulatedYasmPattern.begin(), simulatedYasmPattern.end()});
        CodeRange decodedCodes;
        std::atomic_bool completed{false};
        std::atomic_uint groups{0};
        unsigned measuredGroups = 0;
        orbita::Orbita decoder;
        const auto& m16Address = checks.front().addresses.front();
        decoder.setChannels({{m16Address, {}, {}}});
        decoder.setGroupCallback([&](const auto& words, int group, int cycle) {
            decodedYasm.consume(words, group, cycle);
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
        require(decodedYasm.passed(), "synthetic M16 did not reproduce the YASM sequence");
        require(completed && decodedCodes.count == 2048 && decodedCodes.matches(512, 512),
                "addressed M16 samples changed after the interface gate");
        require(decoder.getValueByAddress(m16Address) == 512, "M16 channel snapshot changed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
