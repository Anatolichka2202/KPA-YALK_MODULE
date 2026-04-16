#ifndef ORBITA_DEVICE_INTERFACE_HPP
#define ORBITA_DEVICE_INTERFACE_HPP

#include <cstdint>

namespace orbita {

class DeviceInterface {
public:
    virtual ~DeviceInterface() = default;

    virtual bool init() = 0;
    virtual bool startStream() = 0;
    virtual bool stopStream() = 0;
    virtual int readSamples(int16_t* buffer, int max_samples, int timeout_ms) = 0;
    virtual bool setParam(const char* param, double value) = 0;
    virtual const char* getInfo() const = 0;
};

} // namespace orbita

#endif // ORBITA_DEVICE_INTERFACE_HPP
