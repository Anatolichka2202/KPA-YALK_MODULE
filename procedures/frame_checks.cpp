#include "frame_checks.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
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
    if (word % 4 == 2)
        return simulatedYasmPattern[(word / 4) % simulatedYasmPattern.size()] << 1;
    // У отсчётов ЯГР и МКО разные слоты. В чётных словах воспроизводим байты
    // RS-485 из ЯФК. Индекс word от нуля.
    if (word % 2 == 0) return 512 << 1;
    return (counterByte((word / 4) & counterLimit, word % 4 == 3) << 3) | 0x04;
}

YasmCheck::YasmCheck(std::vector<unsigned> expected) : expected_(std::move(expected))
{
    if (expected_.size() < 2 || expected_.size() > groupWords
        || std::any_of(expected_.begin(), expected_.end(), [](unsigned code) { return code > counterLimit; })
        || std::all_of(expected_.begin(), expected_.end(), [this](unsigned code) { return code == expected_.front(); }))
        throw std::invalid_argument("Нужна последовательность ЯСМ с хотя бы двумя разными 10-битными кодами");
}

YasmCheck YasmCheck::fromBusWords(const std::vector<uint16_t>& words)
{
    if (words.empty() || words.size() > groupWords / 2)
        throw std::invalid_argument("Неверная длина данных теста ЯСМ");
    std::vector<unsigned> littleEndian, bigEndian;
    littleEndian.reserve(words.size() * 2);
    bigEndian.reserve(words.size() * 2);
    for (const auto word : words) {
        littleEndian.push_back(word & 0xFF);
        littleEndian.push_back(word >> 8);
        bigEndian.push_back(word >> 8);
        bigEndian.push_back(word & 0xFF);
    }
    YasmCheck result(std::move(littleEndian));
    // .bcfrm описывает 16-разрядные слова контроллера. Сравниваем байтовое
    // содержимое всего теста, допуская установленный в исполнении порядок байтов.
    result.alternate_ = std::move(bigEndian);
    return result;
}

void YasmCheck::consume(const std::vector<uint16_t>& words, int group, int cycle)
{
    if (passed_) return;
    if (words.size() != groupWords || group < 1 || group > 32 || cycle < 1 || cycle > 4) {
        recent_.clear();
        lastOrdinal_ = -1;
        return;
    }
    const int ordinal = (cycle - 1) * 32 + group - 1;
    if (lastOrdinal_ >= 0 && ordinal != (lastOrdinal_ + 1) % 128) recent_.clear();
    lastOrdinal_ = ordinal;
    // M16P1A22, 65536 слотов/с. Первый слот — третье слово группы,
    // затем каждое четвёртое. MAIN.v хранит десять бит ЯСМ в разрядах 10:1.
    for (std::size_t slot = 2; slot < words.size(); slot += 4) {
        unsigned code = (words[slot] >> 1) & counterLimit;
        if (!alternate_.empty()) {
            // main_fifo.v ЯСМ: бит 0 отмечает командное слово (также он
            // установлен в заполнении 0x155), биты 9:2 несут байт данных.
            if (code & 1) continue;
            code >>= 2;
        }
        recent_.push_back(code);
        if (recent_.size() > expected_.size()) recent_.pop_front();
        if (recent_.size() == expected_.size()
            && (std::equal(recent_.begin(), recent_.end(), expected_.begin())
                || (!alternate_.empty() && std::equal(recent_.begin(), recent_.end(), alternate_.begin())))) {
            passed_ = true;
            return;
        }
    }
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
        check.bits = node["bits"].as<unsigned>(10);
        if (node["channel_names"])
            check.channelNames = node["channel_names"].as<std::vector<std::string>>();
        const auto fieldShift = node["shift"].as<unsigned>(1);
        if (check.number < 1 || !numbers.insert(check.number).second
            || check.title.empty() || check.requirement.empty()
            || check.cell.empty() || check.bits < 1 || check.bits > 10
            || fieldShift > 10 || fieldShift + check.bits > 11
            || (!check.channelNames.empty() && check.channelNames.size() != check.addresses.size()))
            throw std::runtime_error("Неверные поля строки № " + std::to_string(check.number));

        if (const auto spec = node["scale"]) {
            VoltageScale scale{spec["min_v"].as<double>(), spec["max_v"].as<double>(),
                               spec["min_code"].as<unsigned>(), spec["max_code"].as<unsigned>(), {}};
            if (spec["tolerance_percent"]) scale.tolerancePercent = spec["tolerance_percent"].as<double>();
            if (!std::isfinite(scale.minVolts) || !std::isfinite(scale.maxVolts)
                || scale.minVolts >= scale.maxVolts || scale.minCode >= scale.maxCode
                || scale.maxCode >= (1u << check.bits) || check.bits == 1
                || (scale.tolerancePercent && (!std::isfinite(*scale.tolerancePercent)
                    || *scale.tolerancePercent <= 0 || *scale.tolerancePercent > 100)))
                throw std::runtime_error("Неверная шкала строки № " + std::to_string(check.number));
            check.scale = scale;
        }
        std::set<std::string> addresses;
        for (const auto& address : check.addresses) {
            const auto layout = orbita::m16GroupChannel(address);
            if (!layout || !addresses.insert(address).second)
                throw std::runtime_error("Недопустимый или повторный адрес M16: " + address);
            check.layouts.push_back(*layout);
            auto shift = fieldShift;
            if (check.bits == 1) {
                const auto suffix = address.rfind("T05P");
                if (suffix == std::string::npos || suffix + 6 != address.size()
                    || address[suffix + 4] != '0'
                    || address.back() < '1' || address.back() > '8')
                    throw std::runtime_error("Для сигнала ЯГР требуется адрес T05P01–T05P08: " + address);
                // P01 — старший бит полезного кода. ЯФК кладёт байт сигналов
                // в биты 10:3 слова M16, поэтому младшему биту P08 отвечает бит 3.
                shift = 11 - static_cast<unsigned>(address.back() - '0');
            }
            check.shifts.push_back(shift);
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
        const auto payload = words[word] & 0x7FF;
        const auto value = (payload >> check.shifts[channel]) & mask;
        if (payload == 0x2AA) ++fillCount;
        minimum = count ? std::min(minimum, value) : value;
        maximum = count ? std::max(maximum, value) : value;
        ++count;
    }
}

bool CodeRange::matches(unsigned expectedMin, unsigned expectedMax) const
{
    return count && minimum >= expectedMin && maximum <= expectedMax;
}

double VoltageScale::volts(unsigned code) const
{
    return minVolts + (static_cast<double>(code) - minCode)
        * (maxVolts - minVolts) / (maxCode - minCode);
}

VoltageComparison compareVoltage(const VoltageScale& scale, const CodeRange& codes,
                                 double before, double after)
{
    if (!codes.count || !std::isfinite(before) || !std::isfinite(after))
        throw std::invalid_argument("Для сравнения нужны отсчёты кадра и два показания В7");
    VoltageComparison result{scale.volts(codes.minimum), scale.volts(codes.maximum), 0.0, {}};
    // Сравниваем крайние отсчёты кадра с обоими показаниями В7, чтобы
    // единичный выброс или изменение опорного уровня не скрылись при усреднении.
    result.maximumErrorVolts = std::max({
        std::abs(result.minimumVolts - before), std::abs(result.maximumVolts - before),
        std::abs(result.minimumVolts - after), std::abs(result.maximumVolts - after)});
    if (scale.tolerancePercent)
        result.toleranceVolts = (scale.maxVolts - scale.minVolts) * *scale.tolerancePercent / 100.0;
    return result;
}

} // namespace miltech::brp
