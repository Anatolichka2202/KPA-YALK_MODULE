/**
 * @file decoder_factory.h
 * @brief Фабрика для создания декодера по информативности.
 */

#ifndef ORBITA_DECODER_FACTORY_H
#define ORBITA_DECODER_FACTORY_H

#include <memory>
#include "frame_decoder.h"

namespace orbita {

std::unique_ptr<FrameDecoder> createFrameDecoder(int informativnost);

} // namespace orbita

#endif // ORBITA_DECODER_FACTORY_H
