/**
 * @file frame_decoder_m8.h
 * @brief Декодер для информативностей M08, M04, M02, M01.
 *
 * Реализует поиск маркера фразы по длительности импульса (аналог UnitMoth.pas).
 * Размер группы: M08=1024, M04=512, M02=256, M01=128.
 */

#ifndef ORBITA_FRAME_DECODER_M8_H
#define ORBITA_FRAME_DECODER_M8_H

#include "frame_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

// Создание декодера для M08/04/02/01. informativnost = 1,2,4,8.
orbita_frame_decoder_t* orbita_frame_decoder_m8_create(int informativnost);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_FRAME_DECODER_M8_H
