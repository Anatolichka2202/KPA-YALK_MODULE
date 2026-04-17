#include "frame_decoder_m16.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include "../core/logger.h"
#include <QDebug>
namespace orbita {

FrameDecoderM16::FrameDecoderM16()
    : fifo_(FIFO_SIZE, 0)
    , write_pos_(0)
    , read_pos_(0)
    , count_(0)
    , first_frase_found_(false)
    , point_count_(0)
    , word_num_(1)
    , frase_num_(1)
    , group_num_(1)
    , cikl_num_(1)
    , group_word_count_(1)
    , cod_str_(0)
    , group_buffer_(GROUP_SIZE, 0)
    , start_write_group_(true)
    , fl_sinx_c_(false)
    , fl_kadr_(false)
    , marker_num_group_(0)
    , marker_group_(0)
    , count_even_frase_(0)
    , phrase_errors_(0)
    , group_errors_(0)
    , total_phrases_(0)
    , total_groups_(0)
    , count_for_mf_(0)
    , count_for_mg_(0)
    , has_new_group_(false)
{
    reset();

    group_buffer_.resize(GROUP_SIZE);

    start_write_group_ = true;
    fl_kadr_ = true;
}

void FrameDecoderM16::pushBit(uint8_t bit) {
    fifo_[write_pos_] = bit ? 1 : 0;
    write_pos_ = (write_pos_ + 1) % FIFO_SIZE;
    if (count_ < FIFO_SIZE) ++count_;
    else {
        // Переполнение – сдвигаем чтение
        read_pos_ = (read_pos_ + 1) % FIFO_SIZE;
    }
}

uint8_t FrameDecoderM16::popBit() {
    if (count_ == 0) return 0;
    uint8_t bit = fifo_[read_pos_];
    read_pos_ = (read_pos_ + 1) % FIFO_SIZE;
    --count_;
    return bit;
}

uint8_t FrameDecoderM16::peekBit(size_t offset) const {
    if (offset >= count_) return 0;
    size_t pos = (read_pos_ + offset) % FIFO_SIZE;
    return fifo_[pos];
}

uint8_t FrameDecoderM16::peekAbsolute(size_t pos) const {
    return fifo_[pos % FIFO_SIZE];
}

void FrameDecoderM16::skipBits(size_t n) {
    if (n >= count_) {
        read_pos_ = write_pos_;
        count_ = 0;
    } else {
        read_pos_ = (read_pos_ + n) % FIFO_SIZE;
        count_ -= n;
    }
}

void FrameDecoderM16::rewindBits(size_t n) {
    if (n >= count_) {
        read_pos_ = write_pos_;
        count_ = 0;
    } else {
        if (n <= read_pos_) read_pos_ -= n;
        else read_pos_ = FIFO_SIZE - (n - read_pos_);
        count_ += n;
    }
}

size_t FrameDecoderM16::available() const {
    return count_;
}

void FrameDecoderM16::fillBitInWord() {
    uint8_t bit = popBit();
    cod_str_ = (cod_str_ << 1) | bit;
    qDebug() << "bit:" << (int)bit << " cod_str:" << cod_str_;
}

void FrameDecoderM16::fillArrayGroup() {
    group_buffer_[group_word_count_ - 1] = cod_str_;
    ++group_word_count_;
     qDebug() << "fillArrayGroup: group_word_count=" << group_word_count_;
}

void FrameDecoderM16::collectMarkNumGroup() {
    // Сбор маркера номера группы на чётных фразах 2,4,6,8,10,12,14
    if (frase_num_ == 2 || frase_num_ == 4 || frase_num_ == 6 ||
        frase_num_ == 8 || frase_num_ == 10 || frase_num_ == 12 || frase_num_ == 14) {
        if (cod_str_ & 0x800) {
            marker_num_group_ = (marker_num_group_ << 1) | 1;
        } else {
            marker_num_group_ = (marker_num_group_ << 1);
        }
        if (frase_num_ == 14) marker_num_group_ = 0;
    }
}

void FrameDecoderM16::collectMarkGroup() {
    // Сбор маркера группы из 12-го бита каждой чётной фразы
    if ((frase_num_ & 1) == 0) {
        if (cod_str_ & 0x800) {
            marker_group_ = (marker_group_ << 1) | 1;
        } else {
            marker_group_ = (marker_group_ << 1);
        }
    }
}

void FrameDecoderM16::wordProcessing() {
    // Начало новой фразы
    if (word_num_ == 1) {
        ++frase_num_;
        if (frase_num_ > 128) frase_num_ = 1;

        // Маркер кадра: frase 16, группа 1, 12-й бит = 1
        if (frase_num_ == 16 && group_num_ == 1) {
            if (cod_str_ & 0x800) fl_kadr_ = true;
        }
    }

    // Для чётных фраз собираем маркеры
    if ((frase_num_ & 1) == 0) {
        collectMarkNumGroup();
        collectMarkGroup();
        ++count_even_frase_;

        if (marker_group_ == 114 || marker_group_ == 141) {
            if (count_even_frase_ != 64) ++group_errors_;
            count_even_frase_ = 0;

            if (marker_group_ == 114) {
                ++group_num_;
                if (group_num_ > 32) group_num_ = 1;
                marker_group_ = 0;
                ++count_for_mg_;
                if (count_for_mg_ >= 32) {
                    total_groups_ += count_for_mg_;
                    count_for_mg_ = 0;
                }
            } else if (marker_group_ == 141) {
                ++cikl_num_;
                if (cikl_num_ > 4) cikl_num_ = 1;
                marker_group_ = 0;
                if (fl_kadr_ && cikl_num_ == 1) {
                    fl_sinx_c_ = true;
                }
            }
        }
    }

    // Запись слова в группу
    if (start_write_group_) {
        fillArrayGroup();
        if (group_word_count_ > static_cast<int>(GROUP_SIZE)) {
            has_new_group_ = true;
            if (callback_) callback_(group_buffer_);
            group_word_count_ = 1;
            ++total_groups_;
        }
    }

    cod_str_ = 0;
    ++word_num_;
    if (word_num_ > 16) word_num_ = 1;

    // Отладка каждые 100 слов
    if (group_word_count_ % 100 == 0) {
        qDebug() << "group_word_count:" << group_word_count_
                 << "frase:" << frase_num_
                 << "group:" << group_num_
                 << "cikl:" << cikl_num_;
    }
}

void FrameDecoderM16::analyseFrase() {
    for (int i = 0; i < 12; ++i) {
        fillBitInWord();
        if (i == 11) wordProcessing();
    }
    point_count_ -= 12;
    if (point_count_ <= 0) {
        first_frase_found_ = false;
    }
}

void FrameDecoderM16::searchFirstFraseMarker() {
    if (available() < 384) return;

    uint8_t b0   = peekBit(0);
    uint8_t b24  = peekBit(24);
    uint8_t b48  = peekBit(48);
    uint8_t b72  = peekBit(72);
    uint8_t b96  = peekBit(96);
    uint8_t b120 = peekBit(120);
    uint8_t b144 = peekBit(144);
    uint8_t b168 = peekBit(168);
    uint8_t b216 = peekBit(216);
    uint8_t b240 = peekBit(240);
    uint8_t b264 = peekBit(264);
    uint8_t b288 = peekBit(288);
    uint8_t b312 = peekBit(312);
    uint8_t b336 = peekBit(336);
    uint8_t b360 = peekBit(360);

    if (b0 == 0 && b24 == 1 && b48 == 1 && b72 == 1 && b96 == 1 &&
        b120 == 0 && b144 == 0 && b168 == 0 && b216 == 1 &&
        b240 == 0 && b264 == 0 && b288 == 1 && b312 == 1 && b336 == 0 && b360 == 1) {
       // qDebug() << "!!! First frase marker found at read_pos =" << read_pos_;
        first_frase_found_ = true;
        point_count_ = 383;
        rewindBits(1);
        ++count_for_mf_;
        if (count_for_mf_ >= 127) {
            total_phrases_ += count_for_mf_;
            count_for_mf_ = 0;
        }
    } else {
        static int dbg = 0;
        if (++dbg % 10000 == 0) {
           // qDebug() << "Searching... first bits:"
                   //  << (int)b0 << (int)peekBit(1) << (int)peekBit(2) << (int)peekBit(3);
        skipBits(1);
        ++phrase_errors_;
        ++count_for_mf_;
        if (count_for_mf_ >= 127) {
            total_phrases_ += count_for_mf_;
            count_for_mf_ = 0;
        }
        }
    }
}
void FrameDecoderM16::feedBits(const uint8_t* bits, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < count; ++i) {
        // Используем уже отлаженную логику feedBit
        pushBit(bits[i]);
        while (available() >= 384) {
            if (!first_frase_found_) {
                searchFirstFraseMarker();
            } else {
                analyseFrase();
                if (!first_frase_found_) break;
            }
        }
    }
}

void FrameDecoderM16::feedBit(uint8_t bit) {
    std::lock_guard<std::mutex> lock(mutex_);
    pushBit(bit);
    while (available() >= 384) {
        if (!first_frase_found_) {
            searchFirstFraseMarker();
        } else {
            analyseFrase();
            if (!first_frase_found_) break;
        }
    }
}

bool FrameDecoderM16::hasGroup() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return has_new_group_;
}

std::vector<uint16_t> FrameDecoderM16::getGroup() {
    std::lock_guard<std::mutex> lock(mutex_);
    has_new_group_ = false;
    return group_buffer_;
}

void FrameDecoderM16::getErrors(int& phrase_error_percent, int& group_error_percent) const {
    std::lock_guard<std::mutex> lock(mutex_);
    phrase_error_percent = (total_phrases_ > 0) ? (phrase_errors_ * 100 / total_phrases_) : 0;
    group_error_percent = (total_groups_ > 0) ? (group_errors_ * 100 / total_groups_) : 0;
}

void FrameDecoderM16::reset() {
    // Очищаем FIFO
    write_pos_ = read_pos_ = count_ = 0;
    first_frase_found_ = false;
    point_count_ = 0;
    word_num_ = 1;
    frase_num_ = 1;
    group_num_ = 1;
    cikl_num_ = 1;
    group_word_count_ = 1;
    cod_str_ = 0;
    start_write_group_ = true;
    fl_sinx_c_ = false;
    fl_kadr_ = false;
    marker_num_group_ = 0;
    marker_group_ = 0;
    count_even_frase_ = 0;
    phrase_errors_ = 0;
    group_errors_ = 0;
    total_phrases_ = 0;
    total_groups_ = 0;
    count_for_mf_ = 0;
    count_for_mg_ = 0;
    has_new_group_ = false;
    std::fill(group_buffer_.begin(), group_buffer_.end(), 0);
}

} // namespace orbita
