/**
 * @file visa_device.h
 * @brief Обёртка для работы с VISA-приборами (вольтметр, генератор).
 */

#ifndef ORBITA_VISA_DEVICE_H
#define ORBITA_VISA_DEVICE_H

#include "device_interface.h"
#include <string>
#include <windows.h>

// Определения VISA (упрощённо)
typedef unsigned long ViSession;
typedef unsigned long ViStatus;
typedef unsigned long ViUInt32;
typedef char ViChar;
#define VI_NULL 0
#define VI_SUCCESS 0

class VISADevice : public DeviceInterface {
public:
    VISADevice();
    ~VISADevice() override;

    bool init() override;
    bool startStream() override;
    bool stopStream() override;
    int readSamples(int16_t* buffer, int max_samples, int timeout_ms) override;
    bool setParam(const char* param, double value) override;
    const char* getInfo() const override;

    // Функции для вольтметра
    bool open(const char* resource);
    void close();
    std::string query(const char* command);
    bool write(const char* command);
    std::string read();

private:
    // Типы функций VISA
    typedef ViStatus (*viOpenDefaultRM_t)(ViSession* sesn);
    typedef ViStatus (*viFindRsrc_t)(ViSession sesn, const ViChar* expr, ViSession* vi, ViUInt32* retCnt, ViChar* desc);
    typedef ViStatus (*viOpen_t)(ViSession sesn, const ViChar* name, ViUInt32 mode, ViUInt32 timeout, ViSession* vi);
    typedef ViStatus (*viClose_t)(ViSession vi);
    typedef ViStatus (*viWrite_t)(ViSession vi, const ViChar* buf, ViUInt32 len, ViUInt32* retVal);
    typedef ViStatus (*viRead_t)(ViSession vi, ViChar* buf, ViUInt32 len, ViUInt32* retVal);
    typedef ViStatus (*viSetAttribute_t)(ViSession vi, ViUInt32 attr, ViUInt32 value);

    HMODULE visa_handle;
    ViSession default_rm;
    ViSession instrument;
    std::string info_string;
    bool is_open;

    bool loadVisaLibrary();
    void unloadVisaLibrary();
};

#endif // ORBITA_VISA_DEVICE_H
