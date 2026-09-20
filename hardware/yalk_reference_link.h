#pragma once

#include "hardware/stand_config.h"

#include <chrono>
#include <functional>
#include <memory>

namespace tu::hardware {

class YalkReferenceLink final {
public:
    using Checkpoint = std::function<void()>;

    explicit YalkReferenceLink(YalkUdpConfig config);
    ~YalkReferenceLink();

    YalkReferenceLink(const YalkReferenceLink&) = delete;
    YalkReferenceLink& operator=(const YalkReferenceLink&) = delete;

    // Выполняет подтверждённую последовательность ROKT ЯЛК и ждёт свежий
    // reference204 кадр. При отсутствии кадра повторяет полную инициализацию
    // до общего deadline.
    bool restartUntilReady(std::chrono::milliseconds timeout,
                           const Checkpoint& checkpoint);

    // Ждёт следующий reference204 кадр уже запущенного потока.
    bool waitNextReference(std::chrono::milliseconds timeout,
                           const Checkpoint& checkpoint);

    void stop() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tu::hardware
