#include "frame_decoder_m8.h"
#include "fifo_buffer.h"
#include <cstdlib>
#include <cstring>
#include <cmath>

struct orbita_frame_decoder_m8 {
    orbita_fifo_t fifo;
    int informativnost;          // 1,2,4,8
    // Параметры
    int group_size;
    int words_per_frase;         // количество слов в одной фразе (1,2,4,8)
    double mark_koef;
    int min_size_between;
    // Состояние
    int frase_mark_found;        // флаг нахождения первого маркера
    int count_point_mr_to_mr;
    int q_search_flag;
    int flag_count_frase;        // разрешение записи слов
    int flag_count_group;        // флаг для сбора маркера группы
    int buf_mark_group;
    int buf_mark_circle;
    // Счётчики
    int word_num;
    int frase_num;
    int group_num;
    int cikl_num;
    int group_word_count;        // 1..group_size
    uint16_t cod_str;
    // Буферы
    uint16_t* group_buffer;
    // Статистика
    int count_for_mf;
    int count_error_mf;
    int count_for_mg;
    int count_error_mg;
    int total_phrases;
    int total_groups;
    // Callback
    orbita_decoder_callback_t callback;
    void* user_data;
    int has_new_group;

       int count_even_frase_mg_to_mg;
};

// Вспомогательные функции
static int test_mf_ones(orbita_frame_decoder_m8* dec, int cur_point, int num_ones) {
    // Проверка, что начиная с cur_point (смещение назад) есть num_ones единиц
    int pos = cur_point;
    for (int i = 0; i < num_ones; ++i) {
        pos--;
        if (pos < 0) pos = FIFO_SIZE - 1;
    }
    for (int i = 0; i < num_ones; ++i) {
        uint8_t bit = orbita_fifo_peek_absolute(&dec->fifo, pos);
        if (bit != 1) return 0;
        pos = (pos + 1) % FIFO_SIZE;
    }
    return 1;
}

static int test_mf_null(orbita_frame_decoder_m8* dec, int cur_point, int num_null) {
    // Проверка, что после cur_point идёт num_null нулей
    int pos = cur_point;
    for (int i = 0; i < num_null; ++i) {
        uint8_t bit = orbita_fifo_peek_absolute(&dec->fifo, pos);
        if (bit != 0) return 0;
        pos = (pos + 1) % FIFO_SIZE;
    }
    return 1;
}

static int qtest_marker(orbita_frame_decoder_m8* dec, int beg_point, int point_counter) {
    // Быстрая проверка маркера: point_counter единиц, затем point_counter нулей
    if (!test_mf_ones(dec, beg_point, point_counter)) return 0;
    if (!test_mf_null(dec, beg_point, point_counter)) return 0;
    return 1;
}

static int qfind_frase_mark(orbita_frame_decoder_m8* dec) {
    // Проверка маркера на текущей позиции с шириной widthPartOfMF (3 или 6...)
    int width = (dec->informativnost == 8) ? 3 : (dec->informativnost == 4) ? 6 : (dec->informativnost == 2) ? 12 : 25;
    return qtest_marker(dec, dec->fifo.read_pos, width);
}

static int find_frase_mark(orbita_frame_decoder_m8* dec) {
    // Поиск маркера фразы (аналог FindFraseMark в Delphi)
    int down_to_up = 0;
    int size_frase = 0;
    int num_point = 0;
    int search_ok = 0;
    int max_search = dec->min_size_between * 2;

    for (int i = 0; i < max_search; ++i) {
        uint8_t current = orbita_fifo_pop(&dec->fifo);
        size_frase++;
        // Ищем переход 0->1
        if (!down_to_up) {
            uint8_t next = orbita_fifo_peek(&dec->fifo, 0);
            if (current == 0 && next == 1) {
                down_to_up = 1;
            }
        }
        if (down_to_up) {
            num_point++;
            uint8_t next = orbita_fifo_peek(&dec->fifo, 0);
            if ((current == 0 && next == 1) || (current == 1 && next == 0)) {
                // Проверяем, находится ли отношение num_point / mark_koef в пределах 0.25..0.75
                double ratio = num_point / dec->mark_koef;
                double frac = ratio - floor(ratio);
                if (frac >= 0.25 && frac <= 0.75) {
                    search_ok = 1;
                    break;
                }
            }
        }
    }

    if (search_ok) {
        return size_frase;
    } else {
        return -1;
    }
}

static void calc_word_buf(orbita_frame_decoder_m8* dec, double* count_step, double step_to_trans, uint16_t* word_buf) {
    for (int bit = 11; bit >= 0; --bit) {
        int idx = (int)(*count_step);
        uint8_t b = orbita_fifo_peek_absolute(&dec->fifo, idx);
        if (b) *word_buf |= (1 << bit);
        *count_step += step_to_trans;
        if (*count_step >= FIFO_SIZE) *count_step -= FIFO_SIZE;
    }
}

static void test_collect_mg(orbita_frame_decoder_m8* dec) {
    if ((dec->buf_mark_group & 0xFF) == 114) {
        dec->count_even_frase_mg_to_mg = 0;
        dec->group_num++;
        if (dec->group_num > 32) dec->group_num = 1;
        dec->flag_count_group = 1;
        dec->buf_mark_group = 0;
        // Статистика
        dec->count_for_mg++;
        if (dec->count_for_mg >= 32) {
            dec->total_groups += dec->count_for_mg;
            dec->count_for_mg = 0;
        }
    }
}

static void test_collect_mc(orbita_frame_decoder_m8* dec) {
    if ((dec->buf_mark_group & 0xFF) == 141) {
        dec->cikl_num++;
        if (dec->cikl_num > 4) dec->cikl_num = 1;
        dec->buf_mark_circle = 0;
        dec->flag_count_group = 1;
    }
}

static void even_frase_analyze(orbita_frame_decoder_m8* dec, uint16_t word_buf) {
    if (dec->word_num == dec->words_per_frase + 1) {
        dec->count_even_frase_mg_to_mg++;
        // Собираем buf_mark_group из 12-го бита
        if (word_buf & 0x800) {
            dec->buf_mark_group = (dec->buf_mark_group << 1) | 1;
        } else {
            dec->buf_mark_group = (dec->buf_mark_group << 1);
        }
        test_collect_mg(dec);
        test_collect_mc(dec);
    }
}

static void fill_mas_group(orbita_frame_decoder_m8* dec, int count_point_to_prev_m, int current_mark_beg) {
    int num_word_in_byte = dec->words_per_frase * 2; // слов между маркерами (2 фразы)
    double step_to_trans = (double)count_point_to_prev_m / num_word_in_byte / 12.0;
    double count_step = current_mark_beg;

    dec->word_num = 1;
    for (int w = 0; w < num_word_in_byte; ++w) {
        uint16_t word_buf = 0;
        calc_word_buf(dec, &count_step, step_to_trans, &word_buf);
        // Обработка начала фразы
        if (w == 0 || w == dec->words_per_frase) {
            dec->frase_num++;
            if (dec->frase_num > 128) dec->frase_num = 1;
        }
        // Чётная фраза
        if ((dec->frase_num & 1) == 0) {
            even_frase_analyze(dec, word_buf);
        }
        if (dec->flag_count_frase) {
            dec->group_buffer[dec->group_word_count - 1] = word_buf & 0xFFF;
            dec->group_word_count++;
            if (dec->group_word_count > dec->group_size) {
                dec->has_new_group = 1;
                if (dec->callback) {
                    dec->callback((orbita_frame_decoder_t*)dec, dec->group_buffer, dec->group_size, dec->user_data);
                }
                dec->group_word_count = 1;
                dec->total_groups++;
            }
        }
        if (w == num_word_in_byte - 1 && dec->buf_mark_group == 114) {
            dec->flag_count_frase = 1;
        }
        dec->word_num++;
        if (dec->word_num > num_word_in_byte) dec->word_num = 1;
    }
}

// Публичные функции
orbita_frame_decoder_t* orbita_frame_decoder_m8_create(int informativnost) {
    orbita_frame_decoder_m8* dec = (orbita_frame_decoder_m8*)calloc(1, sizeof(orbita_frame_decoder_m8));
    if (!dec) return nullptr;
    orbita_fifo_init(&dec->fifo);
    dec->informativnost = informativnost;
    switch (informativnost) {
    case 8:
        dec->group_size = 1024;
        dec->words_per_frase = 8;
        dec->mark_koef = 6.357828776;
        dec->min_size_between = 1220;
        break;
    case 4:
        dec->group_size = 512;
        dec->words_per_frase = 4;
        dec->mark_koef = 12.715657552;
        dec->min_size_between = 1220;
        break;
    case 2:
        dec->group_size = 256;
        dec->words_per_frase = 2;
        dec->mark_koef = 25.431315104;
        dec->min_size_between = 1220;
        break;
    case 1:
        dec->group_size = 128;
        dec->words_per_frase = 1;
        dec->mark_koef = 50.862630020;
        dec->min_size_between = 1220;
        break;
    default:
        free(dec);
        return nullptr;
    }
    dec->group_buffer = (uint16_t*)malloc(dec->group_size * sizeof(uint16_t));
    if (!dec->group_buffer) {
        free(dec);
        return nullptr;
    }
    // Инициализация полей
    dec->frase_mark_found = 0;
    dec->q_search_flag = 0;
    dec->flag_count_frase = 0;
    dec->flag_count_group = 0;
    dec->buf_mark_group = 0;
    dec->buf_mark_circle = 0;
    dec->word_num = 1;
    dec->frase_num = 1;
    dec->group_num = 1;
    dec->cikl_num = 1;
    dec->group_word_count = 1;
    dec->count_for_mf = 0;
    dec->count_error_mf = 0;
    dec->count_for_mg = 0;
    dec->count_error_mg = 0;
    dec->total_phrases = 0;
    dec->total_groups = 0;
    dec->callback = nullptr;
    dec->user_data = nullptr;
    dec->has_new_group = 0;
    return (orbita_frame_decoder_t*)dec;
}

void orbita_frame_decoder_destroy(orbita_frame_decoder_t* dec) {
    if (!dec) return;
    orbita_frame_decoder_m8* d = (orbita_frame_decoder_m8*)dec;
    free(d->group_buffer);
    free(d);
}

void orbita_frame_decoder_set_callback(orbita_frame_decoder_t* dec, orbita_decoder_callback_t callback, void* user_data) {
    orbita_frame_decoder_m8* d = (orbita_frame_decoder_m8*)dec;
    if (d) {
        d->callback = callback;
        d->user_data = user_data;
    }
}

void orbita_frame_decoder_feed_bit(orbita_frame_decoder_t* dec, uint8_t bit) {
    orbita_frame_decoder_m8* d = (orbita_frame_decoder_m8*)dec;
    if (!d) return;
    orbita_fifo_push(&d->fifo, bit);
    while (orbita_fifo_available(&d->fifo) >= d->min_size_between * 2) {
        if (!d->frase_mark_found) {
            int res = find_frase_mark(d);
            if (res != -1) {
                d->count_point_mr_to_mr = res;
                d->frase_mark_found = 1;
                d->count_point_mr_to_mr = 0;
            }
        } else {
            if (!d->q_search_flag) {
                d->q_search_flag = 1;
            } else {
                if (qfind_frase_mark(d)) {
                    fill_mas_group(d, d->min_size_between, d->fifo.read_pos);
                } else {
                    int res = find_frase_mark(d);
                    if (res != -1) {
                        fill_mas_group(d, res, d->fifo.read_pos);
                    }
                }
            }
        }
    }
}

int orbita_frame_decoder_has_group(orbita_frame_decoder_t* dec) {
    orbita_frame_decoder_m8* d = (orbita_frame_decoder_m8*)dec;
    return d ? d->has_new_group : 0;
}

const uint16_t* orbita_frame_decoder_get_group(orbita_frame_decoder_t* dec, size_t* out_word_count) {
    orbita_frame_decoder_m8* d = (orbita_frame_decoder_m8*)dec;
    if (!d) return nullptr;
    if (out_word_count) *out_word_count = d->group_size;
    d->has_new_group = 0;
    return d->group_buffer;
}

void orbita_frame_decoder_get_errors(orbita_frame_decoder_t* dec, int* phrase_error_percent, int* group_error_percent) {
    orbita_frame_decoder_m8* d = (orbita_frame_decoder_m8*)dec;
    if (!d) {
        if (phrase_error_percent) *phrase_error_percent = 0;
        if (group_error_percent) *group_error_percent = 0;
        return;
    }
    if (phrase_error_percent) {
        *phrase_error_percent = (d->total_phrases > 0) ? (d->count_error_mf * 100 / d->total_phrases) : 0;
    }
    if (group_error_percent) {
        *group_error_percent = (d->total_groups > 0) ? (d->count_error_mg * 100 / d->total_groups) : 0;
    }
}

void orbita_frame_decoder_reset(orbita_frame_decoder_t* dec) {
    orbita_frame_decoder_m8* d = (orbita_frame_decoder_m8*)dec;
    if (!d) return;
    orbita_fifo_reset(&d->fifo);
    d->frase_mark_found = 0;
    d->q_search_flag = 0;
    d->flag_count_frase = 0;
    d->flag_count_group = 0;
    d->buf_mark_group = 0;
    d->buf_mark_circle = 0;
    d->word_num = 1;
    d->frase_num = 1;
    d->group_num = 1;
    d->cikl_num = 1;
    d->group_word_count = 1;
    d->count_for_mf = 0;
    d->count_error_mf = 0;
    d->count_for_mg = 0;
    d->count_error_mg = 0;
    d->total_phrases = 0;
    d->total_groups = 0;
    d->has_new_group = 0;
}
