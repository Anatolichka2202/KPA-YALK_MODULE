#include "decoder_factory.h"
#include "frame_decoder_m16.h"
#include "frame_decoder_m8.h"
#include <stdexcept>

namespace orbita {

std::unique_ptr<FrameDecoder> createFrameDecoder(int informativnost) {
    switch (informativnost) {
    case 16:
        return std::make_unique<FrameDecoderM16>();
    case 8:
    case 4:
    case 2:
    case 1:
        return std::make_unique<FrameDecoderM8>(informativnost);
    default:
        throw std::runtime_error("Unsupported informativnost: " + std::to_string(informativnost));
    }
}

} // namespace orbita
