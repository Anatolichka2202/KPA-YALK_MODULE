/**
 * @file frame_decoder.h
 * @brief Абстрактный интерфейс декодера телеметрического кадра.
 *
 * Декодер получает биты (0/1), ищет маркеры синхронизации, собирает 12-битные слова.
 * При накоплении полной группы вызывает callback.
 */

#ifndef ORBITA_FRAME_DECODER_H
#define ORBITA_FRAME_DECODER_H

#include <vector>
#include <cstdint>

namespace orbita {

class FrameDecoder {
public:
    virtual ~FrameDecoder() = default;

    // Основной метод обработки (читает биты из внешнего буфера)
    virtual void process() = 0;

    // Проверка, есть ли готовая группа
    virtual bool hasGroup() const = 0;

    // Получить последнюю готовую группу (11-битные слова)
    virtual std::vector<uint16_t> getGroup() = 0;

    // Получить проценты ошибок (маркер фразы и маркер группы)
    virtual void getErrors(int& phrase_err, int& group_err) const = 0;

    // Сброс состояния декодера
    virtual void reset() = 0;
};

} // namespace orbita

#endif
