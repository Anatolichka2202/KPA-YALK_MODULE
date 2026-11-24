#include "frame_checks.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <set>
#include <stdexcept>

namespace miltech::brp {
namespace {

uint8_t counterByte(unsigned value, bool high)
{
    return static_cast<uint8_t>(high ? 0x80 | ((value >> 5) & 0x1F)
                                     : 0x40 | (value & 0x1F));
}

} // namespace

std::vector<uint8_t> counterBytes(unsigned& next, unsigned count)
{
    std::vector<uint8_t> bytes;
    bytes.reserve(2 * count);
    for (unsigned i = 0; i < count; ++i) {
        bytes.push_back(counterByte(next, false));
        bytes.push_back(counterByte(next, true));
        next = (next + 1) & counterLimit;
    }
    return bytes;
}

uint16_t simulatedWord(std::size_t word)
{
    // Нечётные слова M16 отданы известным аналоговым адресам. В чётных
    // воспроизводим размещение байтов RS-485 из ЯФК. Индекс word от нуля.
    if (word % 2 == 0) return 512 << 1;
    return (counterByte((word / 4) & counterLimit, word % 4 == 3) << 3) | 0x04;
}

void CounterCheck::interrupt()
{
    // Уже подтверждённое наблюдение остаётся результатом этой сессии.
    // Разрыв до достижения критерия требует новую непрерывную последовательность.
    if (passed()) return;
    low_.reset();
    previous_.reset();
    contiguous_ = 0;
    trace_.clear();
}

void CounterCheck::consume(const std::vector<uint16_t>& words, int group, int cycle)
{
    if (passed()) return;
    if (words.size() != groupWords || group < 1 || group > 32 || cycle < 1 || cycle > 4) {
        interrupt();
        group_ = cycle_ = 0;
        return;
    }
    const int expectedGroup = group_ % 32 + 1;
    const int expectedCycle = group_ == 32 ? cycle_ % 4 + 1 : cycle_;
    if (group_ && (group != expectedGroup || cycle != expectedCycle)) interrupt();
    group_ = group;
    cycle_ = cycle;

    // ROM_485 версии Actual последовательно выбирает чётные слова M16
    // (нечётные индексы). MAIN помещает байт в биты 10:3, признак данных — в 2,
    // признак паузы перед принятым байтом — в 1. Пустой слот не содержит байта.
    for (std::size_t index = 1; index < words.size(); index += 2) {
        const auto word = words[index];
        if (!(word & 0x04)) break;
        const auto byte = (word >> 3) & 0xFF;
        if ((byte & 0xE0) == 0x40) {
            if (low_) interrupt();
            low_ = byte & 0x1F;
        } else if ((byte & 0xE0) == 0x80 && low_) {
            const auto value = ((byte & 0x1F) << 5) | *low_;
            low_.reset();
            if (previous_ && value != ((*previous_ + 1) & counterLimit)) interrupt();
            previous_ = value;
            ++contiguous_;
            trace_.push_back(value);
            if (passed()) return;
        } else {
            interrupt();
        }
    }
}

std::vector<FrameCheck> loadFrameChecks(const std::string& yaml)
{
    const auto root = YAML::Load(yaml);
    if (root["schema"].as<int>() != 1 || !root["checks"].IsSequence())
        throw std::runtime_error("Неверная схема списка кадровых проверок БРП");
    std::set<int> numbers;
    std::vector<FrameCheck> checks;
    for (const auto& node : root["checks"]) {
        FrameCheck check{};
        check.number = node["number"].as<int>();
        check.title = node["title"].as<std::string>();
        check.requirement = node["tu"].as<std::string>();
        check.cell = node["cell"].as<std::string>();
        check.note = node["note"].as<std::string>("");
        if (node["addresses"]) check.addresses = node["addresses"].as<std::vector<std::string>>();
        check.shift = node["shift"].as<unsigned>(1);
        check.bits = node["bits"].as<unsigned>(10);
        if (check.number < 1 || !numbers.insert(check.number).second
            || check.title.empty() || check.requirement.empty()
            || check.cell.empty() || check.bits < 1 || check.bits > 10
            || check.shift > 10 || check.shift + check.bits > 11)
            throw std::runtime_error("Неверные поля строки № " + std::to_string(check.number));

        std::set<std::string> addresses;
        for (const auto& address : check.addresses) {
            const auto layout = orbita::m16GroupChannel(address);
            if (!layout || !addresses.insert(address).second)
                throw std::runtime_error("Недопустимый или повторный адрес M16: " + address);
            check.layouts.push_back(*layout);
        }
        checks.push_back(std::move(check));
    }
    if (checks.empty()) throw std::runtime_error("Список кадровых проверок пуст");
    return checks;
}

void CodeRange::consume(const std::vector<uint16_t>& words,
                        const FrameCheck& check, std::size_t channel)
{
    if (words.size() != groupWords) return;
    const auto layout = check.layouts.at(channel);
    const auto mask = (1u << check.bits) - 1;
    for (std::size_t word = layout.firstWord; word < words.size(); word += layout.stride) {
        const auto value = (words[word] >> check.shift) & mask;
        minimum = count ? std::min(minimum, value) : value;
        maximum = count ? std::max(maximum, value) : value;
        ++count;
    }
}

bool CodeRange::matches(unsigned expectedMin, unsigned expectedMax) const
{
    return count && minimum >= expectedMin && maximum <= expectedMax;
}

} // namespace miltech::brp
