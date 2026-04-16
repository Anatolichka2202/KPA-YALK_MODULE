//тест декодера М16.
//работаем в связке с устрйоством.
//смотрим типы данных, входные/выходные данные


// test_decoder_m16.cpp
#include "decoder/frame_decoder_m16.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace orbita;

int main() {
    // 1. Генерируем тестовую группу из 2048 слов (12 бит)
    const size_t GROUP_SIZE = 2048;
    std::vector<uint16_t> original_group(GROUP_SIZE);
    for (size_t i = 0; i < GROUP_SIZE; ++i) {
        original_group[i] = static_cast<uint16_t>(i & 0xFFF);
    }

    // 2. Формируем битовый поток для одной группы
    std::vector<uint8_t> bits;
    // Маркер фразы (15 бит)
    const std::vector<uint8_t> marker = {0,1,1,1,1,0,0,0,1,0,0,1,1,0,1};
    size_t word_idx = 0;

    for (int frase = 0; frase < 128; ++frase) {
        // Маркер перед каждой нечётной фразой (1,3,5,...)
        if ((frase & 1) == 0) {
            bits.insert(bits.end(), marker.begin(), marker.end());
        }
        // 16 слов во фразе
        for (int w = 0; w < 16; ++w) {
            if (word_idx >= original_group.size()) break;
            uint16_t val = original_group[word_idx++];
            for (int bit = 11; bit >= 0; --bit) {
                bits.push_back((val >> bit) & 1);
            }
        }
    }
    assert(word_idx == original_group.size());

    // 3. Декодер
    FrameDecoderM16 decoder;
    bool group_received = false;
    std::vector<uint16_t> decoded_group;
    decoder.setCallback([&](const std::vector<uint16_t>& group) {
        group_received = true;
        decoded_group = group;
    });

    decoder.feedBits(bits.data(), bits.size());

    // 4. Проверки
    assert(group_received);
    assert(decoded_group.size() == GROUP_SIZE);

    // Сравниваем первые 10 и последние 10 слов
    for (size_t i = 0; i < 10; ++i) {
        assert(decoded_group[i] == original_group[i]);
    }
    for (size_t i = GROUP_SIZE - 10; i < GROUP_SIZE; ++i) {
        assert(decoded_group[i] == original_group[i]);
    }

    // Проверка ошибок синхронизации
    int phrase_err = 0, group_err = 0;
    decoder.getErrors(phrase_err, group_err);
    assert(phrase_err == 0);
    assert(group_err == 0);

    std::cout << "Decoder synthetic test passed.\n";
    return 0;
}
