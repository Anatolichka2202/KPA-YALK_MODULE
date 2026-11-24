#include "frame_decoder_m8.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace orbita {
namespace {

double samplesPerBit(double sampleRate)
{
    // Для проверки устойчивых уровней маркера нужно как минимум два
    // отсчёта на половину бита. Номинал Delphi — АЦП 10 Мвыб/с.
    constexpr double bitRate = 1'572'864;
    if (!std::isfinite(sampleRate) || sampleRate < 4 * bitRate || sampleRate > 1e9)
        throw std::invalid_argument("Недопустимая частота АЦП для Орбита M8");
    return sampleRate / bitRate;
}

int percentage(uint64_t errors, uint64_t checks)
{
    return checks ? static_cast<int>(std::lround(100.0 * errors / checks)) : 0;
}

} // namespace

FrameDecoderM8::FrameDecoderM8(double sampleRate)
    : samplesPerBit_(samplesPerBit(sampleRate)),
      samplesPerPair_(samplesPerBit_ * bitsPerPair),
      halfWidth_(static_cast<std::size_t>(samplesPerBit_ / 2))
{
    groupWords_.reserve(wordsPerGroup);
    reset();
}

void FrameDecoderM8::reset()
{
    samples_.clear();
    threshold_.reset();
    scan_ = halfWidth_;
    discardedSamples_ = 0;
    lastRise_.reset();
    marker_.reset();
    groupWords_.clear();
    markerBits_ = 0;
    group_ = cycle_ = 0;
    phraseChecks_ = phraseErrors_ = groupChecks_ = groupErrors_ = decodedGroups_ = 0;
    writer_.reset();
    recordBlock_ = 0;
    recordCycleOpen_ = false;
}

void FrameDecoderM8::appendSamples(const std::vector<int16_t>& samples, bool inverted)
{
    if (inverted_ != inverted) {
        // Смена полярности разрывает текущую пару. Данные в старой
        // полярности и новой полярности в одну группу не попадут.
        inverted_ = inverted;
        samples_.clear();
        scan_ = halfWidth_;
        marker_.reset();
        lastRise_.reset();
        loseGroupSync();
    }
    samples_.insert(samples_.end(), samples.begin(), samples.end());
}

bool FrameDecoderM8::high(std::size_t index) const
{
    const bool above = samples_[index] >= *threshold_;
    return above != inverted_;
}

bool FrameDecoderM8::isFallingMarker(std::size_t edge) const
{
    // UnitMoth.QtestMarker проверяет устойчивые уровни с обеих сторон
    // спада. Для АЦП 10 Мвыб/с здесь проверяются три отсчёта.
    for (std::size_t offset = 0; offset < halfWidth_; ++offset) {
        if (!high(edge - 1 - offset) || high(edge + offset)) return false;
    }
    return true;
}

void FrameDecoderM8::process()
{
    if (!threshold_) {
        if (samples_.size() < static_cast<std::size_t>(std::ceil(samplesPerPair_))) return;
        const auto extremes = std::minmax_element(samples_.begin(), samples_.end());
        if (*extremes.first == *extremes.second) {
            samples_.clear();
            return;
        }
        const int minimum = *extremes.first;
        threshold_ = minimum + (*extremes.second - minimum + 1) / 2;
    }

    // Окно в четверть бита отделяет спад маркера от соседней обычной
    // границы символа. При 10 Мвыб/с пара занимает около 1221 отсчёта.
    const double tolerance = samplesPerBit_ / 4;
    while (scan_ + halfWidth_ <= samples_.size()) {
        const auto edge = scan_++;
        if (marker_ && edge - *marker_ > samplesPerPair_ + tolerance) {
            ++phraseChecks_;
            ++phraseErrors_;
            marker_.reset();
            loseGroupSync();
        }
        if (high(edge - 1) == high(edge)) continue;
        if (high(edge)) {
            lastRise_ = discardedSamples_ + edge;
            continue;
        }
        if (!isFallingMarker(edge)) continue;

        if (marker_) {
            if (edge - *marker_ >= samplesPerPair_ - tolerance) {
                ++phraseChecks_;
                decodePhrasePair(*marker_, edge);
                marker_ = edge;
            }
            continue;
        }
        if (!lastRise_) continue;

        // UnitMoth.FindFraseMark ищет переход, смещённый на полбита
        // относительно обычной границы. После захвата работаем по расстоянию
        // между соседними маркерами, а не по длительности единичного импульса.
        const double period = (discardedSamples_ + edge - *lastRise_) / samplesPerBit_;
        const double fraction = period - std::floor(period);
        if (fraction >= 0.25 && fraction <= 0.75) marker_ = edge;
    }

    // Оставляем незавершённую пару и запас слева от следующего фронта.
    // Это позволяет передавать произвольные порции отсчётов АЦП.
    const auto keepFrom = std::min(scan_ - halfWidth_, marker_.value_or(scan_));
    samples_.erase(samples_.begin(), samples_.begin() + keepFrom);
    discardedSamples_ += keepFrom;
    scan_ -= keepFrom;
    if (marker_) *marker_ -= keepFrom;
}

void FrameDecoderM8::decodePhrasePair(std::size_t begin, std::size_t end)
{
    std::array<uint16_t, 2 * wordsPerPhrase> words{};
    const double step = static_cast<double>(end - begin) / bitsPerPair;
    for (unsigned index = 0; index < bitsPerPair; ++index) {
        // Интерполируем по двум соседним маркерам, как FillMasGroup в
        // Delphi. Старший бит передаётся первым.
        const auto sample = begin + static_cast<std::size_t>(std::lround(index * step));
        auto& word = words[index / bitsPerWord];
        word = static_cast<uint16_t>((word << 1) | (high(sample) ? 1 : 0));
    }

    // Первое слово чётной фразы несёт очередной бит МГ / МЦ в старшем разряде.
    markerBits_ = static_cast<uint8_t>((markerBits_ << 1) | (words[wordsPerPhrase] >> 11));
    if (!group_) {
        if (markerBits_ == cycleMarker) {
            group_ = cycle_ = 1;
            markerBits_ = 0;
        }
        return;
    }

    groupWords_.insert(groupWords_.end(), words.begin(), words.end());
    if (groupWords_.size() != wordsPerGroup) return;
    const bool cycleEnd = group_ == 32;
    ++groupChecks_;
    if (markerBits_ != (cycleEnd ? cycleMarker : groupMarker)) {
        ++groupErrors_;
        loseGroupSync();
        return;
    }

    publishGroup();
    groupWords_.clear();
    markerBits_ = 0;
    if (cycleEnd) {
        group_ = 1;
        cycle_ = cycle_ % 4 + 1;
    } else {
        ++group_;
    }
}

void FrameDecoderM8::loseGroupSync()
{
    if (!groupWords_.empty() && groupWords_.size() < wordsPerGroup) {
        ++groupChecks_;
        ++groupErrors_;
    }
    groupWords_.clear();
    markerBits_ = 0;
    group_ = cycle_ = 0;
    recordCycleOpen_ = false;
}

void FrameDecoderM8::publishGroup()
{
    ++decodedGroups_;
    if (writer_) {
        if (group_ == 1) {
            ++recordBlock_;
            recordCycleOpen_ = true;
        }
        if (recordCycleOpen_) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - recordStart_).count();
            writer_->writeBlock(recordBlock_, static_cast<uint32_t>(elapsed), groupWords_);
        }
    }
    if (callback_) callback_(groupWords_, group_, cycle_);
}

void FrameDecoderM8::getErrors(int& phraseError, int& groupError) const
{
    phraseError = percentage(phraseErrors_, phraseChecks_);
    groupError = percentage(groupErrors_, groupChecks_);
}

void FrameDecoderM8::startTlm(const std::string& filename)
{
    if (writer_) throw std::runtime_error("Запись уже ведётся");
    auto writer = std::make_unique<TlmWriter>(filename, 8);
    if (!writer->isOpen()) throw std::runtime_error("Не удалось открыть файл записи");
    writer_ = std::move(writer);
    recordBlock_ = 0;
    recordStart_ = std::chrono::steady_clock::now();
    recordCycleOpen_ = false;
}

void FrameDecoderM8::stopTlm()
{
    if (!writer_) return;
    const bool saved = writer_->finish();
    writer_.reset();
    if (!saved) throw std::runtime_error("Не удалось завершить запись потока M8");
}

} // namespace orbita
