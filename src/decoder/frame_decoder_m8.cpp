#include "frame_decoder_m8.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace orbita {

// ----------------------------------------------------------------------
// Конструктор
// ----------------------------------------------------------------------
FrameDecoderM8::FrameDecoderM8(int informativnost)
    : fifo_(FIFO_SIZE, 0)
    , write_pos_(0)
    , read_pos_(0)
    , count_(0)
    , informativnost_(informativnost)
    , frase_mark_found_(false)
    , count_point_mr_to_mr_(0)
    , q_search_flag_(false)
    , flag_count_frase_(false)
    , flag_count_group_(false)
    , buf_mark_group_(0)
    , buf_mark_circle_(0)
    , word_num_(1)
    , frase_num_(1)
    , group_num_(1)
    , cikl_num_(1)
    , group_word_count_(1)
    , cod_str_(0)
    , count_for_mf_(0)
    , count_error_mf_(0)
    , count_for_mg_(0)
    , count_error_mg_(0)
    , total_phrases_(0)
    , total_groups_(0)
    , count_even_frase_mg_to_mg_(0)
    , has_new_group_(false)
{
    switch (informativnost) {
    case 8:
        group_size_ = 1024;
        words_per_frase_ = 8;
        mark_koef_ = 6.357828776;
        min_size_between_ = 1220;
        break;
    case 4:
        group_size_ = 512;
        words_per_frase_ = 4;
        mark_koef_ = 12.715657552;
        min_size_between_ = 1220;
        break;
    case 2:
        group_size_ = 256;
        words_per_frase_ = 2;
        mark_koef_ = 25.431315104;
        min_size_between_ = 1220;
        break;
    case 1:
        group_size_ = 128;
        words_per_frase_ = 1;
        mark_koef_ = 50.862630020;
        min_size_between_ = 1220;
        break;
    default:
        throw std::runtime_error("Invalid informativnost for M8 decoder");
    }
    group_buffer_.resize(group_size_);
    reset();
}

// ----------------------------------------------------------------------
// Методы FIFO
// ----------------------------------------------------------------------
void FrameDecoderM8::pushBit(uint8_t bit) {
    fifo_[write_pos_] = bit ? 1 : 0;
    write_pos_ = (write_pos_ + 1) % FIFO_SIZE;
    if (count_ < FIFO_SIZE) ++count_;
    else {
        read_pos_ = (read_pos_ + 1) % FIFO_SIZE;
    }
}

uint8_t FrameDecoderM8::popBit() {
    if (count_ == 0) return 0;
    uint8_t bit = fifo_[read_pos_];
    read_pos_ = (read_pos_ + 1) % FIFO_SIZE;
    --count_;
    return bit;
}

uint8_t FrameDecoderM8::peekBit(size_t offset) const {
    if (offset >= count_) return 0;
    size_t pos = (read_pos_ + offset) % FIFO_SIZE;
    return fifo_[pos];
}

uint8_t FrameDecoderM8::peekAbsolute(size_t pos) const {
    return fifo_[pos % FIFO_SIZE];
}

void FrameDecoderM8::skipBits(size_t n) {
    if (n >= count_) {
        read_pos_ = write_pos_;
        count_ = 0;
    } else {
        read_pos_ = (read_pos_ + n) % FIFO_SIZE;
        count_ -= n;
    }
}

void FrameDecoderM8::rewindBits(size_t n) {
    if (n >= count_) {
        read_pos_ = write_pos_;
        count_ = 0;
    } else {
        if (n <= read_pos_) read_pos_ -= n;
        else read_pos_ = FIFO_SIZE - (n - read_pos_);
        count_ += n;
    }
}

size_t FrameDecoderM8::available() const {
    return count_;
}

// ----------------------------------------------------------------------
// Вспомогательные методы для поиска маркеров
// ----------------------------------------------------------------------
bool FrameDecoderM8::testMfOnes(int cur_point, int num_ones) {
    // Перемещаемся назад на num_ones позиций
    int pos = cur_point;
    for (int i = 0; i < num_ones; ++i) {
        --pos;
        if (pos < 0) pos = static_cast<int>(FIFO_SIZE) - 1;
    }
    for (int i = 0; i < num_ones; ++i) {
        if (peekAbsolute(static_cast<size_t>(pos)) != 1) return false;
        pos = (pos + 1) % static_cast<int>(FIFO_SIZE);
    }
    return true;
}

bool FrameDecoderM8::testMfNull(int cur_point, int num_null) {
    int pos = cur_point;
    for (int i = 0; i < num_null; ++i) {
        if (peekAbsolute(static_cast<size_t>(pos)) != 0) return false;
        pos = (pos + 1) % static_cast<int>(FIFO_SIZE);
    }
    return true;
}

bool FrameDecoderM8::qtestMarker(int beg_point, int point_counter) {
    return testMfOnes(beg_point, point_counter) && testMfNull(beg_point, point_counter);
}

bool FrameDecoderM8::qfindFraseMark() {
    // Быстрая проверка маркера на текущей позиции
    int width = (informativnost_ == 8) ? 3 : (informativnost_ == 4) ? 6 : (informativnost_ == 2) ? 12 : 25;
    return qtestMarker(static_cast<int>(read_pos_), width);
}

int FrameDecoderM8::findFraseMark() {
    int down_to_up = 0;
    int size_frase = 0;
    int num_point = 0;
    int search_ok = 0;
    int max_search = min_size_between_ * 2;

    for (int i = 0; i < max_search; ++i) {
        uint8_t current = popBit();
        ++size_frase;
        if (!down_to_up) {
            uint8_t next = peekBit(0);
            if (current == 0 && next == 1) {
                down_to_up = 1;
            }
        }
        if (down_to_up) {
            ++num_point;
            uint8_t next = peekBit(0);
            if ((current == 0 && next == 1) || (current == 1 && next == 0)) {
                double ratio = static_cast<double>(num_point) / mark_koef_;
                double frac = ratio - std::floor(ratio);
                if (frac >= 0.25 && frac <= 0.75) {
                    search_ok = 1;
                    break;
                }
            }
        }
    }

    if (search_ok) return size_frase;
    else return -1;
}

// ----------------------------------------------------------------------
// Сбор слова и заполнение группы
// ----------------------------------------------------------------------
void FrameDecoderM8::calcWordBuf(double& count_step, double step_to_trans, uint16_t& word_buf) {
    for (int bit = 11; bit >= 0; --bit) {
        int idx = static_cast<int>(std::floor(count_step));
        uint8_t b = peekAbsolute(static_cast<size_t>(idx));
        if (b) word_buf |= (1 << bit);
        count_step += step_to_trans;
        if (count_step >= static_cast<double>(FIFO_SIZE)) count_step -= FIFO_SIZE;
    }
}

void FrameDecoderM8::testCollectMG() {
    if ((buf_mark_group_ & 0xFF) == 114) { // 0x72
        count_even_frase_mg_to_mg_ = 0;
        ++group_num_;
        if (group_num_ > 32) group_num_ = 1;
        flag_count_group_ = true;
        buf_mark_group_ = 0;
        ++count_for_mg_;
        if (count_for_mg_ >= 32) {
            total_groups_ += count_for_mg_;
            count_for_mg_ = 0;
        }
    }
}

void FrameDecoderM8::testCollectMC() {
    if ((buf_mark_group_ & 0xFF) == 141) { // 0x8D
        ++cikl_num_;
        if (cikl_num_ > 4) cikl_num_ = 1;
        buf_mark_circle_ = 0;
        flag_count_group_ = true;
    }
}

void FrameDecoderM8::evenFraseAnalyze(uint16_t word_buf) {
    if (word_num_ == words_per_frase_ + 1) {
        ++count_even_frase_mg_to_mg_;
        if (word_buf & 0x800) {
            buf_mark_group_ = (buf_mark_group_ << 1) | 1;
        } else {
            buf_mark_group_ = (buf_mark_group_ << 1);
        }
        testCollectMG();
        testCollectMC();
    }
}

void FrameDecoderM8::fillMasGroup(int count_point_to_prev_m, int current_mark_beg) {
    int num_word_in_byte = words_per_frase_ * 2; // слов между маркерами (2 фразы)
    double step_to_trans = static_cast<double>(count_point_to_prev_m) / static_cast<double>(num_word_in_byte) / 12.0;
    double count_step = static_cast<double>(current_mark_beg);

    word_num_ = 1;
    for (int w = 0; w < num_word_in_byte; ++w) {
        uint16_t word_buf = 0;
        calcWordBuf(count_step, step_to_trans, word_buf);

        // Начало новой фразы
        if (w == 0 || w == words_per_frase_) {
            ++frase_num_;
            if (frase_num_ > 128) frase_num_ = 1;
        }

        // Чётная фраза
        if ((frase_num_ & 1) == 0) {
            evenFraseAnalyze(word_buf);
        }

        if (flag_count_frase_) {
            group_buffer_[group_word_count_ - 1] = word_buf & 0xFFF;
            ++group_word_count_;
            if (group_word_count_ > group_size_) {
                has_new_group_ = true;
                if (0) {
                    //callback_(group_buffer_);
                }
                group_word_count_ = 1;
                ++total_groups_;
            }
        }

        if (w == num_word_in_byte - 1 && (buf_mark_group_ & 0xFF) == 114) {
            flag_count_frase_ = true;
        }

        ++word_num_;
        if (word_num_ > num_word_in_byte) word_num_ = 1;
    }
}

// ----------------------------------------------------------------------
// Публичные методы
// ----------------------------------------------------------------------
void FrameDecoderM8::feedBit(uint8_t bit) {
    pushBit(bit);
    while (available() >= static_cast<size_t>(min_size_between_ * 2)) {
        if (!frase_mark_found_) {
            int res = findFraseMark();
            if (res != -1) {
                count_point_mr_to_mr_ = res;
                frase_mark_found_ = true;
                count_point_mr_to_mr_ = 0;
            }
        } else {
            if (!q_search_flag_) {
                q_search_flag_ = true;
            } else {
                if (qfindFraseMark()) {
                    fillMasGroup(min_size_between_, static_cast<int>(read_pos_));
                } else {
                    int res = findFraseMark();
                    if (res != -1) {
                        fillMasGroup(res, static_cast<int>(read_pos_));
                    }
                }
            }
        }
    }
}

bool FrameDecoderM8::hasGroup() const {
    return has_new_group_;
}

std::vector<uint16_t> FrameDecoderM8::getGroup() {
    has_new_group_ = false;
    return group_buffer_; // копия
}

void FrameDecoderM8::getErrors(int& phrase_error_percent, int& group_error_percent) const {
    phrase_error_percent = (total_phrases_ > 0) ? (count_error_mf_ * 100 / total_phrases_) : 0;
    group_error_percent = (total_groups_ > 0) ? (count_error_mg_ * 100 / total_groups_) : 0;
}

void FrameDecoderM8::reset() {
    write_pos_ = read_pos_ = count_ = 0;
    frase_mark_found_ = false;
    q_search_flag_ = false;
    flag_count_frase_ = false;
    flag_count_group_ = false;
    buf_mark_group_ = 0;
    buf_mark_circle_ = 0;
    word_num_ = 1;
    frase_num_ = 1;
    group_num_ = 1;
    cikl_num_ = 1;
    group_word_count_ = 1;
    cod_str_ = 0;
    count_for_mf_ = 0;
    count_error_mf_ = 0;
    count_for_mg_ = 0;
    count_error_mg_ = 0;
    total_phrases_ = 0;
    total_groups_ = 0;
    count_even_frase_mg_to_mg_ = 0;
    has_new_group_ = false;
    std::fill(group_buffer_.begin(), group_buffer_.end(), 0);
}

} // namespace orbita
