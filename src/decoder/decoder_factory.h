/**
 * @file decoder_factory.h
 * @brief Фабрика для создания декодера по информативности.
 */

#ifndef ORBITA_DECODER_FACTORY_H
#define ORBITA_DECODER_FACTORY_H

#include "frame_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

// Создание декодера для заданной информативности (1,2,4,8,16)
orbita_frame_decoder_t* orbita_decoder_create(int informativnost);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_DECODER_FACTORY_H
