#include "decoder_factory.h"
#include "frame_decoder_m16.h"
// #include "frame_decoder_m8.h" // будет добавлен позже

orbita_frame_decoder_t* orbita_decoder_create(int informativnost) {
    switch (informativnost) {
    case 16: return orbita_frame_decoder_m16_create();
    // case 8:  return orbita_frame_decoder_m8_create(8);
    // case 4:  return orbita_frame_decoder_m8_create(4);
    // case 2:  return orbita_frame_decoder_m8_create(2);
    // case 1:  return orbita_frame_decoder_m8_create(1);
    default: return nullptr;
    }
}
