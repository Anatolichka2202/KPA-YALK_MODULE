#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orbita {

// Внутренняя граница между декодером «Орбиты» и конкретным устройством ввода.
// Реализации могут работать с E20-10, другим АЦП, файлом записи или имитатором.
class ISampleSource {
public:
    using SamplesCallback = std::function<void(const std::vector<int16_t>&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    virtual ~ISampleSource() = default;

    virtual bool init(int slot, int channel, double sampleRateKHz) = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    virtual void setSamplesCallback(SamplesCallback callback) = 0;
    virtual void setErrorCallback(ErrorCallback callback) = 0;
};

} // namespace orbita
