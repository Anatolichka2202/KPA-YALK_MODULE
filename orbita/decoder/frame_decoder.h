/**
 * @file frame_decoder.h
 * @brief Абстрактный интерфейс декодера телеметрического кадра.
 *
 * Декодеры выделяют 12-битные слова и передают полную группу через callback.
 * M16 читает восстановленные биты, M8 синхронизируется по отсчётам АЦП.
 */

#ifndef ORBITA_FRAME_DECODER_H
#define ORBITA_FRAME_DECODER_H

#include <vector>
#include <cstdint>
#include <string>
#include "../include/orbita.h"

namespace orbita {

class FrameDecoder {
public:
    virtual ~FrameDecoder() = default;

    // Основной метод обработки (читает биты из внешнего буфера)
    virtual void process() = 0;

    // Получить проценты ошибок (маркер фразы и маркер группы)
   virtual void getErrors(int& phrase_err, int& group_err) const = 0;

    // Сброс состояния декодера
    virtual void reset() = 0;
    virtual void setGroupCallback(GroupCallback callback) = 0;
    virtual uint64_t getTotalGroups() const = 0;
    virtual void startTlm(const std::string& filename) = 0;
    virtual void stopTlm() = 0;
    virtual bool isTlmActive() const = 0;
};

} // namespace orbita

#endif
