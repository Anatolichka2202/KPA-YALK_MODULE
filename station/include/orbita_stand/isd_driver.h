#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace orbita::stand {

enum class IsdDriverState {
    Known,
    Unknown,
    Recovering
};

struct IsdDriverOps {
    std::function<std::string()> probe;
    std::function<void()> reset;
    std::function<void()> prepareYalk;
    std::function<void(unsigned, unsigned, bool)> setSwitch;
    std::function<void(unsigned, unsigned, bool)> setAnalog;
    std::function<void(unsigned, double)> setYalkVoltage;
    std::function<void(unsigned)> disableYalkOutput;
};

class IsdDriver final {
public:
    explicit IsdDriver(IsdDriverOps ops, std::size_t traceCapacity = 256);
    ~IsdDriver();

    std::string probe();
    void reset(const std::string& owner = "system");
    void prepareYalk(const std::string& owner = "system");
    void setSwitch(unsigned type, unsigned channel, bool enabled,
                   const std::string& owner = "legacy");
    void setAnalog(unsigned channel, unsigned value, bool enabled,
                   const std::string& owner = "legacy");
    void setYalkVoltage(unsigned channel, double volts,
                        const std::string& owner = "legacy");
    void disableYalkOutput(unsigned channel,
                           const std::string& owner = "legacy");

    void releaseOwner(const std::string& owner);
    void safeStopAll() noexcept;

    IsdDriverState state() const noexcept;
    std::string statusText() const;
    std::string traceText() const;
    void clearTrace();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

const char* toString(IsdDriverState state) noexcept;

} // namespace orbita::stand
