/**
 * @file frame_decoder_m8.h
 * @brief Декодер для информативностей M08, M04, M02, M01.
 */

#ifndef ORBITA_FRAME_DECODER_M8_H
#define ORBITA_FRAME_DECODER_M8_H

#include "frame_decoder.h"
#include <vector>
#include <cstdint>

namespace orbita {

class FrameDecoderM8 : public FrameDecoder {
public:
    explicit FrameDecoderM8(int informativnost); // 1,2,4,8
    ~FrameDecoderM8() override = default;

    void feedBit(uint8_t bit) ;
    bool hasGroup() const override;
    std::vector<uint16_t> getGroup() override;
    void getErrors(int& phrase_error_percent, int& group_error_percent) const override;
    void reset() override;

private:
    static constexpr size_t FIFO_SIZE = 2500000;
    std::vector<uint8_t> fifo_;
    size_t write_pos_;
    size_t read_pos_;
    size_t count_;

    int informativnost_;      // 1,2,4,8
    int group_size_;          // 128,256,512,1024
    int words_per_frase_;     // 1,2,4,8
    double mark_koef_;        // коэффициент для поиска маркера
    int min_size_between_;    // минимальное расстояние между маркерами (1220)

    // Состояние
    bool frase_mark_found_;
    int count_point_mr_to_mr_;
    bool q_search_flag_;
    bool flag_count_frase_;
    bool flag_count_group_;
    uint64_t buf_mark_group_;
    uint64_t buf_mark_circle_;
    int word_num_;
    int frase_num_;
    int group_num_;
    int cikl_num_;
    int group_word_count_;
    uint16_t cod_str_;

    // Буфер группы
    std::vector<uint16_t> group_buffer_;

    // Статистика
    int count_for_mf_;
    int count_error_mf_;
    int count_for_mg_;
    int count_error_mg_;
    int total_phrases_;
    int total_groups_;
    int count_even_frase_mg_to_mg_;

    bool has_new_group_;

    // Вспомогательные методы FIFO
    void pushBit(uint8_t bit);
    uint8_t popBit();
    uint8_t peekBit(size_t offset) const;
    uint8_t peekAbsolute(size_t pos) const;
    void skipBits(size_t n);
    void rewindBits(size_t n);
    size_t available() const;

    // Методы поиска маркеров
    bool testMfOnes(int cur_point, int num_ones);
    bool testMfNull(int cur_point, int num_null);
    bool qtestMarker(int beg_point, int point_counter);
    bool qfindFraseMark();
    int findFraseMark();
    void calcWordBuf(double& count_step, double step_to_trans, uint16_t& word_buf);
    void testCollectMG();
    void testCollectMC();
    void evenFraseAnalyze(uint16_t word_buf);
    void fillMasGroup(int count_point_to_prev_m, int current_mark_beg);
};

} // namespace orbita

#endif // ORBITA_FRAME_DECODER_M8_H
