/**
 * @file limesdr_device.h
 * @brief Реализация DeviceInterface для LimeSDR (через LimeSuite.dll).
 */
#ifndef ORBITA_LIMESDR_DEVICE_H
#define ORBITA_LIMESDR_DEVICE_H

#include "device_interface.h"
#include <string>
#include <vector>
#include <windows.h>

class LimeSDRDevice : public DeviceInterface {
public:
    LimeSDRDevice();
    ~LimeSDRDevice() override;

    bool init() override;
    bool startStream() override;
    bool stopStream() override;
    int readSamples(int16_t* buffer, int max_samples, int timeout_ms) override;
    bool setParam(const char* param, double value) override;
    const char* getInfo() const override;

    // Установка частоты, усиления
    bool setFrequency(double freq_hz);
    bool setGain(double gain_db);
    bool setSampleRate(double rate_hz);

private:
    // Типы функций LimeSuite
    typedef int (*LMS_Open_t)(void** device, const char* args, void* lib);
    typedef int (*LMS_Init_t)(void* device);
    typedef int (*LMS_SetupStream_t)(void* device, void** stream);
    typedef int (*LMS_StartStream_t)(void* stream);
    typedef int (*LMS_RecvStream_t)(void* stream, void** samples, size_t sample_count, void* meta, unsigned int timeout_ms);
    typedef int (*LMS_StopStream_t)(void* stream);
    typedef int (*LMS_DestroyStream_t)(void* device, void* stream);
    typedef int (*LMS_Close_t)(void* device);
    typedef int (*LMS_EnableChannel_t)(void* device, int dir, size_t channel, bool enable);
    typedef int (*LMS_SetLOFrequency_t)(void* device, int dir, size_t channel, double freq);
    typedef int (*LMS_SetGaindB_t)(void* device, int dir, size_t channel, double gain);
    typedef int (*LMS_SetSampleRate_t)(void* device, float rate, size_t oversample);

    HMODULE limesuite_handle;
    void* device;
    void* rx_stream;
    std::string info_string;
    double frequency_hz;
    double gain_db;
    double sample_rate_hz;
    bool is_streaming;
    HANDLE read_thread;
    DWORD thread_id;
    volatile bool stop_requested;

    bool loadLibrary();
    void unloadLibrary();
    static DWORD WINAPI readThreadProc(LPVOID param);
};

#endif // ORBITA_LIMESDR_DEVICE_H
