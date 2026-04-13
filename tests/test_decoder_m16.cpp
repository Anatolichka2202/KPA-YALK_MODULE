#include "../src/decoder/frame_decoder.h"
#include "../src/decoder/frame_decoder_m16.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_group(orbita_frame_decoder_t* dec, const uint16_t* group, size_t word_count, void* user) {
    static int group_counter = 0;
    printf("Group %d received, words=%zu\n", ++group_counter, word_count);
    // Здесь можно сравнить с эталонным массивом
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <bits_file.bin>\n", argv[0]);
        return 1;
    }
    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }
    orbita_frame_decoder_t* dec = orbita_frame_decoder_m16_create();
    if (!dec) {
        fprintf(stderr, "Failed to create decoder\n");
        fclose(f);
        return 1;
    }
    orbita_frame_decoder_set_callback(dec, on_group, nullptr);

    uint8_t byte;
    while (fread(&byte, 1, 1, f) == 1) {
        // Предполагаем, что в файле каждый байт – это бит (0 или 1)
        orbita_frame_decoder_feed_bit(dec, byte & 1);
    }
    fclose(f);

    int phrase_err, group_err;
    orbita_frame_decoder_get_errors(dec, &phrase_err, &group_err);
    printf("Errors: phrase=%d%%, group=%d%%\n", phrase_err, group_err);

    orbita_frame_decoder_destroy(dec);
    return 0;
}
