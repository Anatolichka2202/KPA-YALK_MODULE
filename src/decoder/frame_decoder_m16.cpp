/**
 * @file frame_decoder_m16.cpp
 * @brief Декодер телеметрии M16 (2048 слов в группе, 128 фраз по 16 слов).
 *
 * Реализация на основе Delphi-класса TDataM16 (UnitM16.pas).
 */

#include "frame_decoder_m16.h"
#include "fifo_buffer.h"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <stdio.h>
// --------------------------------------------------------------
// Внутренняя структура
// --------------------------------------------------------------

struct orbita_frame_decoder_m16 {
    orbita_fifo_t fifo;                 // кольцевой буфер битов
    // Состояние синхронизации
    int first_frase_found;              // аналог firstFraseFl
    int point_count;                    // оставшиеся биты до следующего маркера
    // Счётчики кадра
    int word_num;                       // 1..16
    int frase_num;                      // 1..128
    int group_num;                      // 1..32
    int cikl_num;                       // 1..4
    int group_word_count;               // 1..2048
    uint16_t cod_str;                   // накапливаемое 12-битное слово
    // Буферы данных
    uint16_t* group_buffer;             // masGroupAll (12 бит)
    size_t group_size;                  // = 2048
    // Флаги управления
    int start_write_group;              // разрешение записи в группу
    int fl_sinx_c;                      // синхронизация записи цикла (пока не используется)
    int fl_kadr;                        // маркер кадра найден
    // Сбор маркеров групп/циклов
    uint8_t marker_num_group;           // 8 бит (не используется, но храним)
    uint8_t marker_group;               // 8 бит
    int count_even_frase;               // счётчик чётных фраз (0..64)
    // Статистика ошибок
    int phrase_errors;                  // сбои маркеров фраз за 127 фраз
    int group_errors;                   // сбои маркеров групп за 32 группы
    int total_phrases;                  // всего обработано фраз (для процента)
    int total_groups;                   // всего обработано групп
    int count_for_mf;                   // счётчик для вывода ошибок МФ (каждые 127 фраз)
    int count_for_mg;                   // счётчик для вывода ошибок МГ (каждые 32 группы)
    // Callback
    orbita_decoder_callback_t callback;
    void* user_data;
    int has_new_group;
};

// --------------------------------------------------------------
// Вспомогательные функции (локальные)
// --------------------------------------------------------------

static void fill_bit_in_word(orbita_frame_decoder_m16* dec) {
    uint8_t bit = orbita_fifo_pop(&dec->fifo);
    dec->cod_str = (dec->cod_str << 1) | bit;
}

static void fill_array_group(orbita_frame_decoder_m16* dec) {
    dec->group_buffer[dec->group_word_count - 1] = dec->cod_str;
    dec->group_word_count++;
}

static void collect_mark_num_group(orbita_frame_decoder_m16* dec) {
    // Delphi: если fraseNum = 2,4,6,8,10,12,14 – собираем 8 бит из 12-го бита
    if (dec->frase_num == 2 || dec->frase_num == 4 || dec->frase_num == 6 ||
        dec->frase_num == 8 || dec->frase_num == 10 || dec->frase_num == 12 || dec->frase_num == 14) {
        if (dec->cod_str & 0x800) {
            dec->marker_num_group = (dec->marker_num_group << 1) | 1;
        } else {
            dec->marker_num_group = (dec->marker_num_group << 1);
        }
        if (dec->frase_num == 14) dec->marker_num_group = 0;
    }
}

static void collect_mark_group(orbita_frame_decoder_m16* dec) {
    // Сбор маркера группы из 12-го бита каждой чётной фразы
    if ((dec->frase_num & 1) == 0) {
        if (dec->cod_str & 0x800) {
            dec->marker_group = (dec->marker_group << 1) | 1;
        } else {
            dec->marker_group = (dec->marker_group << 1);
        }
    }
}

static void word_processing(orbita_frame_decoder_m16* dec) {
    // Обработка собранного 12-битного слова
    if (dec->word_num == 1) {
        // Начало новой фразы
        dec->frase_num++;
        if (dec->frase_num > 128) dec->frase_num = 1;

        // Маркер кадра: frase 16, группа 1, 12-й бит = 1
        if (dec->frase_num == 16 && dec->group_num == 1) {
            if (dec->cod_str & 0x800) {
                dec->fl_kadr = 1;
            }
        }

        // Для чётных фраз собираем маркеры
        if ((dec->frase_num & 1) == 0) {
            collect_mark_num_group(dec);
            collect_mark_group(dec);
            dec->count_even_frase++;

            // Проверка маркера группы (114 = 0x72)
            if (dec->marker_group == 114) {
                // Найден маркер группы
                dec->group_num++;
                if (dec->group_num > 32) dec->group_num = 1;
                // Статистика ошибок: если счётчик чётных фраз не 64 – был сбой
                if (dec->count_even_frase != 64) dec->group_errors++;
                dec->count_even_frase = 0;
                dec->marker_group = 0;
                // Увеличиваем счётчик групп для вывода ошибок каждые 32 группы
                dec->count_for_mg++;
                if (dec->count_for_mg >= 32) {
                    // Здесь можно обновить процент ошибок (но проценты считаем отдельно)
                    dec->total_groups += dec->count_for_mg;
                    dec->count_for_mg = 0;
                }
            }
            // Проверка маркера цикла (141 = 0x8D)
            else if (dec->marker_group == 141) {
                dec->cikl_num++;
                if (dec->cikl_num > 4) dec->cikl_num = 1;
                dec->count_even_frase = 0;
                dec->marker_group = 0;
                // Разрешаем запись в цикл, если найден кадр и цикл сброшен
                if (dec->fl_kadr && dec->cikl_num == 1) {
                    dec->fl_sinx_c = 1;
                }
            }
        }
    }

    // Запись слова в группу (если разрешено)
    if (dec->start_write_group) {
        fill_array_group(dec);
        // Здесь можно добавить запись в буфер цикла (пока пропускаем)
        if (dec->group_word_count > (int)dec->group_size) {
            // Группа заполнена
            dec->has_new_group = 1;
            if (dec->callback) {
                dec->callback((orbita_frame_decoder_t*)dec, dec->group_buffer, dec->group_size, dec->user_data);
            }
            // Сброс для следующей группы
            dec->group_word_count = 1;
            dec->total_groups++;
        }
    }

    // Сброс текущего слова
    dec->cod_str = 0;
    // Переход к следующему слову
    dec->word_num++;
    if (dec->word_num > 16) dec->word_num = 1;
}

static void analyse_frase(orbita_frame_decoder_m16* dec) {
    // Разбор одной фразы: 12 бит
    for (int i = 0; i < 12; i++) {
        dec->point_count--;
        fill_bit_in_word(dec);
        if (dec->point_count == 0) {
            dec->first_frase_found = 0;
            break;
        }
        if (i == 11) word_processing(dec);
    }
}

static void search_first_frase_marker(orbita_frame_decoder_m16* dec) {
    // Поиск первого маркера фразы по фиксированной последовательности
    // Согласно Delphi: проверяем биты на смещениях 0,24,48,72,96,120,144,168,216,240,264,288,312,336,360
    // Условие: current (смещение 0) = 0, затем на 24:1, 48:1, 72:1, 96:1, 120:0, 144:0, 168:0, 216:1, 240:0, 264:0, 288:1, 312:1, 336:0, 360:1
    // Если все совпадают – маркер найден.
    if (orbita_fifo_available(&dec->fifo) < 384) return;

    // Считываем биты с нужными смещениями
    uint8_t b0  = orbita_fifo_peek(&dec->fifo, 0);
    uint8_t b24 = orbita_fifo_peek(&dec->fifo, 24);
    uint8_t b48 = orbita_fifo_peek(&dec->fifo, 48);
    uint8_t b72 = orbita_fifo_peek(&dec->fifo, 72);
    uint8_t b96 = orbita_fifo_peek(&dec->fifo, 96);
    uint8_t b120= orbita_fifo_peek(&dec->fifo, 120);
    uint8_t b144= orbita_fifo_peek(&dec->fifo, 144);
    uint8_t b168= orbita_fifo_peek(&dec->fifo, 168);
    uint8_t b216= orbita_fifo_peek(&dec->fifo, 216);
    uint8_t b240= orbita_fifo_peek(&dec->fifo, 240);
    uint8_t b264= orbita_fifo_peek(&dec->fifo, 264);
    uint8_t b288= orbita_fifo_peek(&dec->fifo, 288);
    uint8_t b312= orbita_fifo_peek(&dec->fifo, 312);
    uint8_t b336= orbita_fifo_peek(&dec->fifo, 336);
    uint8_t b360= orbita_fifo_peek(&dec->fifo, 360);

    if (b0 == 0 && b24 == 1 && b48 == 1 && b72 == 1 && b96 == 1 &&
        b120 == 0 && b144 == 0 && b168 == 0 && b216 == 1 &&
        b240 == 0 && b264 == 0 && b288 == 1 && b312 == 1 && b336 == 0 && b360 == 1) {
        // Маркер найден
        dec->first_frase_found = 1;
        dec->point_count = 383;
        orbita_fifo_rewind(&dec->fifo, 1);
        dec->count_for_mf++;
        if (dec->count_for_mf >= 127) {
            dec->total_phrases += dec->count_for_mf;
            dec->count_for_mf = 0;
        }
    } else {
        //  КЛЮЧЕВОЕ ИСПРАВЛЕНИЕ: пропускаем один бит и продолжаем поиск
        orbita_fifo_skip(&dec->fifo, 1);
        dec->phrase_errors++;
        dec->count_for_mf++;
        if (dec->count_for_mf >= 127) {
            dec->total_phrases += dec->count_for_mf;
            dec->count_for_mf = 0;
        }
    }
}

// --------------------------------------------------------------
// Публичные функции
// --------------------------------------------------------------

orbita_frame_decoder_t* orbita_frame_decoder_m16_create(void) {
    orbita_frame_decoder_m16* dec = (orbita_frame_decoder_m16*)calloc(1, sizeof(orbita_frame_decoder_m16));
    if (!dec) return nullptr;
    orbita_fifo_init(&dec->fifo);
    dec->group_size = 2048;
    dec->group_buffer = (uint16_t*)malloc(dec->group_size * sizeof(uint16_t));
    if (!dec->group_buffer) {
        free(dec);
        return nullptr;
    }
    // Инициализация полей
    dec->word_num = 1;
    dec->frase_num = 1;
    dec->group_num = 1;
    dec->cikl_num = 1;
    dec->group_word_count = 1;
    dec->start_write_group = 1;   // В Delphi startWriteMasGroup = true после нахождения кадра? Пока включаем.
    dec->fl_sinx_c = 0;
    dec->fl_kadr = 0;
    dec->marker_num_group = 0;
    dec->marker_group = 0;
    dec->count_even_frase = 0;
    dec->phrase_errors = 0;
    dec->group_errors = 0;
    dec->total_phrases = 0;
    dec->total_groups = 0;
    dec->count_for_mf = 0;
    dec->count_for_mg = 0;
    dec->callback = nullptr;
    dec->user_data = nullptr;
    dec->has_new_group = 0;
    return (orbita_frame_decoder_t*)dec;
}

void orbita_frame_decoder_destroy(orbita_frame_decoder_t* dec) {
    if (!dec) return;
    orbita_frame_decoder_m16* d = (orbita_frame_decoder_m16*)dec;
    free(d->group_buffer);
    free(d);
}

void orbita_frame_decoder_set_callback(orbita_frame_decoder_t* dec, orbita_decoder_callback_t callback, void* user_data) {
    orbita_frame_decoder_m16* d = (orbita_frame_decoder_m16*)dec;
    if (d) {
        d->callback = callback;
        d->user_data = user_data;
    }
}

void orbita_frame_decoder_feed_bit(orbita_frame_decoder_t* dec, uint8_t bit) {
    orbita_frame_decoder_m16* d = (orbita_frame_decoder_m16*)dec;
    if (!d) return;
    orbita_fifo_push(&d->fifo, bit);
    // Основной цикл обработки – пока есть достаточно битов
    while (orbita_fifo_available(&d->fifo) >= 384) {
        if (!d->first_frase_found) {
            search_first_frase_marker(d);
        } else {
            analyse_frase(d);
            if (!d->first_frase_found) break; // потеря синхронизации
        }
    }
}

int orbita_frame_decoder_has_group(orbita_frame_decoder_t* dec) {
    orbita_frame_decoder_m16* d = (orbita_frame_decoder_m16*)dec;
    return d ? d->has_new_group : 0;
}

const uint16_t* orbita_frame_decoder_get_group(orbita_frame_decoder_t* dec, size_t* out_word_count) {
    orbita_frame_decoder_m16* d = (orbita_frame_decoder_m16*)dec;
    if (!d) return nullptr;
    if (out_word_count) *out_word_count = d->group_size;
    d->has_new_group = 0;
    return d->group_buffer;
}

void orbita_frame_decoder_get_errors(orbita_frame_decoder_t* dec, int* phrase_error_percent, int* group_error_percent) {
    orbita_frame_decoder_m16* d = (orbita_frame_decoder_m16*)dec;
    if (!d) {
        if (phrase_error_percent) *phrase_error_percent = 0;
        if (group_error_percent) *group_error_percent = 0;
        return;
    }
    if (phrase_error_percent) {
        *phrase_error_percent = (d->total_phrases > 0) ? (d->phrase_errors * 100 / d->total_phrases) : 0;
    }
    if (group_error_percent) {
        *group_error_percent = (d->total_groups > 0) ? (d->group_errors * 100 / d->total_groups) : 0;
    }
}

void orbita_frame_decoder_reset(orbita_frame_decoder_t* dec) {
    orbita_frame_decoder_m16* d = (orbita_frame_decoder_m16*)dec;
    if (!d) return;
    orbita_fifo_reset(&d->fifo);
    d->first_frase_found = 0;
    d->point_count = 0;
    d->word_num = 1;
    d->frase_num = 1;
    d->group_num = 1;
    d->cikl_num = 1;
    d->group_word_count = 1;
    d->cod_str = 0;
    d->start_write_group = 1;
    d->fl_sinx_c = 0;
    d->fl_kadr = 0;
    d->marker_num_group = 0;
    d->marker_group = 0;
    d->count_even_frase = 0;
    d->phrase_errors = 0;
    d->group_errors = 0;
    d->total_phrases = 0;
    d->total_groups = 0;
    d->count_for_mf = 0;
    d->count_for_mg = 0;
    d->has_new_group = 0;
}
