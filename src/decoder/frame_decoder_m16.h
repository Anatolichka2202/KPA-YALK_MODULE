/**
 * @file frame_decoder_m16.h
 * @brief Декодер для информативности M16.
 *
 * Реализует поиск маркера фразы по фиксированной последовательности битов,
 * сбор 12-битных слов, формирование групп (2048 слов) и циклов (32 группы).
 */

#ifndef ORBITA_FRAME_DECODER_M16_H
#define ORBITA_FRAME_DECODER_M16_H

#include "frame_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

// Создание декодера M16 (вспомогательная функция для фабрики)
orbita_frame_decoder_t* orbita_frame_decoder_m16_create(void);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_FRAME_DECODER_M16_H
