#pragma once

namespace orbita::stand {

// Minimal typed contracts shared by station hardware adapters and procedures.
// They describe generic behaviour only; product-specific YALK/UBSI concepts
// stay in their own domain headers.
class IIsdRouter {
public:
    virtual ~IIsdRouter() = default;
    virtual void reset() = 0;
    virtual void connectChannel(unsigned channel) = 0;
    virtual void disconnectChannel(unsigned channel) = 0;
};

class IVoltageSource {
public:
    virtual ~IVoltageSource() = default;
    virtual void setVoltage(double volts) = 0;
    virtual void outputOn() = 0;
    virtual void outputOff() = 0;
};

class IReferenceVoltmeter {
public:
    virtual ~IReferenceVoltmeter() = default;
    virtual double readVoltage() = 0;
};

class IProcedureWaiter {
public:
    virtual ~IProcedureWaiter() = default;
    virtual void waitMilliseconds(unsigned milliseconds) = 0;
};

} // namespace orbita::stand
