//проверка ядра.
//Работа чистого ядра. подаем эталонные данные.
//смотрим что делает ядро, какие данные он требует и отдает.


#include "orbita.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <thread>
#include <chrono>

// Функция для подачи битов из TLM-файла в ядро (имитация потока)
static void feed_tlm_to_core(orbita_handle_t h, const char* tlm_filename) {
    FILE* f = fopen(tlm_filename, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open TLM file\n");
        return;
    }
    // Пропускаем заголовок (256 байт)
    fseek(f, 256, SEEK_SET);
    uint16_t word;
    long long total_words = 0;
    while (fread(&word, 2, 1, f) == 1) {
        uint16_t data12 = word & 0xFFF;
        for (int bit = 11; bit >= 0; --bit) {
            uint8_t b = (data12 >> bit) & 1;
            // Прямой вызов API для подачи бита (нужно добавить такую функцию)
            // Пока нет, используем внутреннюю функцию через обёртку.
            // Для теста можно добавить orbita_feed_bit в публичный API.
            // Временно используем непубличную функцию.
             orbita_feed_bit(h, b);
        }
        total_words++;
        if (total_words % 10000 == 0) printf("Fed %lld words\n", total_words);
    }
    fclose(f);
    printf("Feeding finished, total words: %lld\n", total_words);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <address_file.txt> <tlm_file.tlm>\n", argv[0]);
        return 1;
    }
    const char* addr_file = argv[1];
    const char* tlm_file = argv[2];

    orbita_handle_t h = orbita_create();
    if (!h) {
        fprintf(stderr, "Failed to create orbita handle\n");
        return 1;
    }

    // Загружаем адреса
    if (orbita_set_channels_from_file(h, addr_file) != 0) {
        fprintf(stderr, "Failed to load addresses\n");
        orbita_destroy(h);
        return 1;
    }

    orbita_start_recording(h, "test_output.tlm");

    // Запускаем ядро
    if (orbita_start(h) != 0) {
        fprintf(stderr, "Failed to start\n");
        orbita_destroy(h);
        return 1;
    }

    // В отдельном потоке подаём биты из TLM
    std::thread feeder(feed_tlm_to_core, h, tlm_file);

    // Основной цикл ожидания данных
    int group_count = 0;
    for (int i = 0; i < 100; ++i) {  // ждём до 100 групп
        int res = orbita_wait_for_data(h, 5000);
        if (res == 0) {
            group_count++;
            printf("New data available (group %d)\n", group_count);
            // Получаем значения первых нескольких каналов
            for (int ch = 0; ch < 5; ++ch) {
                double analog = orbita_get_analog(h, ch);
                printf("  Analog[%d] = %.3f V\n", ch, analog);
            }
        } else if (res == -2) {
            printf("Timeout waiting for data\n");
            break;
        } else {
            printf("Error waiting for data\n");
            break;
        }
    }

    orbita_stop(h);
    feeder.join();
    orbita_stop_recording(h);
    orbita_destroy(h);
    return 0;
}
