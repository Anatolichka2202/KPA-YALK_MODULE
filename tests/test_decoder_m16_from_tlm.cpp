#include "../src/decoder/frame_decoder.h"
#include "../src/decoder/frame_decoder_m16.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static void on_group(orbita_frame_decoder_t* dec, const uint16_t* group, size_t word_count, void* user) {
    static int group_counter = 0;
    group_counter++;
    printf("Group %d received, words=%zu\n", group_counter, word_count);
    if (group_counter <= 5) {
        printf("  First 10 words: ");
        for (size_t i = 0; i < 10 && i < word_count; ++i) {
            printf("%04X ", group[i]);
        }
        printf("\n");
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.tlm>\n", argv[0]);
        return 1;
    }
    const char* filename = argv[1];
    FILE* f = fopen(filename, "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    // Получаем размер файла
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 256, SEEK_SET);
    printf("File size: %ld bytes, skipping 256 header, remaining %ld bytes\n", file_size, file_size - 256);
    fflush(stdout);

    orbita_frame_decoder_t* dec = orbita_frame_decoder_m16_create();
    if (!dec) {
        fprintf(stderr, "Failed to create decoder\n");
        fclose(f);
        return 1;
    }
    orbita_frame_decoder_set_callback(dec, on_group, nullptr);

    long long total_bits = 0;
    long long total_words = 0;

    // Чтение блоками по 65536 слов (каждое слово 2 байта)
    const size_t buf_size = 65536;
    uint16_t* buffer = (uint16_t*)malloc(buf_size * sizeof(uint16_t));
    if (!buffer) {
        fprintf(stderr, "malloc failed\n");
        fclose(f);
        orbita_frame_decoder_destroy(dec);
        return 1;
    }

    printf("Starting to read file in blocks...\n");
    fflush(stdout);

    size_t words_read;
    while ((words_read = fread(buffer, sizeof(uint16_t), buf_size, f)) > 0) {
        for (size_t i = 0; i < words_read; ++i) {
            total_words++;
            if (total_words <= 10 || total_words % 10000 == 0) {
                printf("Word %lld: %04X\n", total_words, buffer[i]);
                fflush(stdout);
            }
            uint16_t val = buffer[i] & 0xFFF;
            for (int bit = 11; bit >= 0; --bit) {
                uint8_t b = (val >> bit) & 1;

                 orbita_frame_decoder_feed_bit(dec, b);
                total_bits++;
            }
        }
        if (total_words > 10000000) {
            printf("Reached 10M words, breaking\n");
            break;
        }
    }
    free(buffer);
    fclose(f);

    printf("Finished reading. Total words: %lld, bits: %lld\n", total_words, total_bits);
    fflush(stdout);

    int phrase_err, group_err;
    orbita_frame_decoder_get_errors(dec, &phrase_err, &group_err);
    printf("Errors: phrase=%d%%, group=%d%%\n", phrase_err, group_err);

    orbita_frame_decoder_destroy(dec);
    return 0;
}
