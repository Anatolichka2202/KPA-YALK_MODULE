/**
 * @file frame_decoder.h
 * @brief Абстрактный интерфейс декодера телеметрического кадра.
 *
 * Декодер получает биты (0/1), ищет маркеры синхронизации, собирает 12-битные слова.
 * При накоплении полной группы вызывает callback.
 */

#ifndef ORBITA_FRAME_DECODER_H
#define ORBITA_FRAME_DECODER_H

#include <cstdint>
#include <functional>
#include <vector>

namespace orbita {

// Тип callback-а для уведомления о готовности группы
using DecoderCallback = std::function<void(const std::vector<uint16_t>& group_words)>;

class FrameDecoder {
public:
    virtual ~FrameDecoder() = default;

    // Подача одного бита (0 или 1)
    virtual void feedBit(uint8_t bit) = 0;

    // Подача массива битов
    virtual void feedBits(const uint8_t* bits, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            feedBit(bits[i]);
        }
    }

    // Проверка, накоплена ли новая группа (если callback не используется)
    virtual bool hasGroup() const = 0;

    // Получить последнюю накопленную группу (12-битные слова)
    virtual std::vector<uint16_t> getGroup() = 0;

    // Получить статистику ошибок синхронизации (проценты)
    virtual void getErrors(int& phrase_error_percent, int& group_error_percent) const = 0;

    // Сброс состояния (при потере сигнала)
    virtual void reset() = 0;

    // Установка callback (если не установлен, группа сохраняется во внутреннем буфере)
    void setCallback(DecoderCallback callback) {
        callback_ = std::move(callback);
    }

protected:
    DecoderCallback callback_;
};

} // namespace orbita

#endif // ORBITA_FRAME_DECODER_H
