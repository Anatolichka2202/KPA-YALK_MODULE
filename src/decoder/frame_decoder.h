/**
 * @file frame_decoder.h
 * @brief Абстрактный интерфейс декодера телеметрического кадра.
 *
 * Декодер получает биты (0/1), ищет маркеры синхронизации, собирает 12-битные слова.
 * При накоплении полной группы вызывает callback или устанавливает флаг.
 */

#ifndef ORBITA_FRAME_DECODER_H
#define ORBITA_FRAME_DECODER_H

#include <stdint.h>
#include <stddef.h>

// Структура-дескриптор декодера (opaque)
typedef struct orbita_frame_decoder orbita_frame_decoder_t;

// Тип callback-а для уведомления о готовности группы
// Параметры: decoder, указатель на массив 12-битных слов, количество слов, пользовательские данные
typedef void (*orbita_decoder_callback_t)(orbita_frame_decoder_t* dec, const uint16_t* group_words, size_t word_count, void* user_data);

// Создание декодера под заданную информативность (1,2,4,8,16)
// Возвращает NULL при ошибке.
orbita_frame_decoder_t* orbita_frame_decoder_create(int informativnost);

// Уничтожение декодера
void orbita_frame_decoder_destroy(orbita_frame_decoder_t* dec);

// Установка callback-а (если не установлен, группа сохраняется во внутреннем буфере)
void orbita_frame_decoder_set_callback(orbita_frame_decoder_t* dec, orbita_decoder_callback_t callback, void* user_data);

// Подача одного бита (0 или 1)
void orbita_frame_decoder_feed_bit(orbita_frame_decoder_t* dec, uint8_t bit);

// Проверка, накоплена ли новая группа (если callback не используется)
int orbita_frame_decoder_has_group(orbita_frame_decoder_t* dec);

// Получить последнюю накопленную группу (12-битные слова). Возвращает указатель на внутренний буфер.
const uint16_t* orbita_frame_decoder_get_group(orbita_frame_decoder_t* dec, size_t* out_word_count);

// Получить статистику ошибок синхронизации (проценты)
void orbita_frame_decoder_get_errors(orbita_frame_decoder_t* dec, int* phrase_error_percent, int* group_error_percent);

// Сброс состояния (при потере сигнала)
void orbita_frame_decoder_reset(orbita_frame_decoder_t* dec);

#endif // ORBITA_FRAME_DECODER_H
