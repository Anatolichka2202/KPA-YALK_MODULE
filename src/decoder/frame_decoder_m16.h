/**
 * @file frame_decoder_m16.h
 * @brief Декодер для информативности M16 (2048 слов в группе, 128 фраз по 16 слов).
 */

#ifndef ORBITA_FRAME_DECODER_M16_H
#define ORBITA_FRAME_DECODER_M16_H

#include "frame_decoder.h"
#include <vector>
#include <cstdint>

namespace orbita {

class FrameDecoderM16 : public FrameDecoder {
public:
    FrameDecoderM16();
    ~FrameDecoderM16() override = default;

    void feedBit(uint8_t bit) override;
    void feedBits(const uint8_t* bits, size_t count) override;
    bool hasGroup() const override;
    std::vector<uint16_t> getGroup() override;
    void getErrors(int& phrase_error_percent, int& group_error_percent) const override;
    void reset() override;

private:
    // Кольцевой буфер битов (храним как вектор uint8_t, размер 2500000)
    static constexpr size_t FIFO_SIZE = 2500000;
    std::vector<uint8_t> fifo_;
    size_t write_pos_;
    size_t read_pos_;
    size_t count_;
    mutable std::mutex mutex_;
    // Состояние синхронизации
    bool first_frase_found_;
    int point_count_;          // оставшиеся биты до следующего маркера

    // Счётчики кадра
    int word_num_;             // 1..16
    int frase_num_;            // 1..128
    int group_num_;            // 1..32
    int cikl_num_;             // 1..4
    int group_word_count_;     // 1..2048
    uint16_t cod_str_;         // накапливаемое 12-битное слово

    // Буфер данных группы (12 бит)
    std::vector<uint16_t> group_buffer_;
    static constexpr size_t GROUP_SIZE = 2048;

    // Флаги управления
    bool start_write_group_;
    bool fl_sinx_c_;           // синхронизация записи цикла
    bool fl_kadr_;             // маркер кадра найден

    // Сбор маркеров групп/циклов
    uint8_t marker_num_group_; // 8 бит
    uint8_t marker_group_;     // 8 бит
    int count_even_frase_;     // счётчик чётных фраз (0..64)

    // Статистика ошибок
    int phrase_errors_;
    int group_errors_;
    int total_phrases_;
    int total_groups_;
    int count_for_mf_;         // счётчик для вывода ошибок МФ (каждые 127 фраз)
    int count_for_mg_;         // счётчик для вывода ошибок МГ (каждые 32 группы)

    // Флаг наличия новой группы (если callback не используется)
    bool has_new_group_;

    // Вспомогательные методы
    void pushBit(uint8_t bit);
    uint8_t popBit();
    uint8_t peekBit(size_t offset) const;
    uint8_t peekAbsolute(size_t pos) const;
    void skipBits(size_t n);
    void rewindBits(size_t n);
    size_t available() const;

    void fillBitInWord();
    void fillArrayGroup();
    void collectMarkNumGroup();
    void collectMarkGroup();
    void wordProcessing();
    void analyseFrase();
    void searchFirstFraseMarker();
};

} // namespace orbita

#endif // ORBITA_FRAME_DECODER_M16_H
