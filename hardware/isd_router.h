#pragma once

#include "hardware/stand_config.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace tu::hardware {

struct IsdRequestTrace {
    std::uint64_t sequence = 0;
    unsigned type = 0;
    unsigned channel = 0;
    std::uint64_t latencyMilliseconds = 0;
    int httpStatus = 0;
    bool timeout = false;
    bool accepted = false;
    std::string path;
    std::string response;
};

class IsdRouter final
{
public:
    using TraceSink = std::function<void(const IsdRequestTrace& trace)>;
    using RecoveryHandler = std::function<bool(const std::string& reason)>;
    using RecoveryRestoredSink = std::function<void()>;

    explicit IsdRouter(IsdConfig config);
    ~IsdRouter();

    IsdRouter(const IsdRouter&) = delete;
    IsdRouter& operator=(const IsdRouter&) = delete;

    std::string probe();
    void setSwitch(unsigned type, unsigned channel, bool enabled);
    void setAnalog(unsigned channel, unsigned value, bool enabled);
    void setYalkVoltage(unsigned channel, double volts);
    void disableYalkOutput(unsigned channel);
    // Observability only.  Commands remain one-shot: the sink cannot cause a
    // retry or alter the acknowledgement decision.
    void setTraceSink(TraceSink sink);
    // Called only when the controller receives no HTTP response from the ISD.
    // Returning true means that the operator restarted the ISD and the router
    // may restore the last confirmed addressed state before continuing.
    void setRecoveryHandler(RecoveryHandler handler);
    void setRecoveryRestoredSink(RecoveryRestoredSink sink);
    void safeStop() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tu::hardware
