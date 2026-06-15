#pragma once

#include <thread>
#include <vector>
#include <cstdint>
#include <atomic>
#include <functional>
#include <windows.h>
#include "Lusbapi.h"

namespace orbita {

class E2010Device {
public:
    E2010Device() = default;
    ~E2010Device();

    E2010Device(const E2010Device&) = delete;
    E2010Device& operator=(const E2010Device&) = delete;

    bool init(int slot = 0, int channel = 0, double sampleRateKHz = 10000.0);
    bool start();
    void stop();

    bool isRunning() const { return isRunning_.load(); }

    // Колбэк вызывается из внутреннего потока чтения при поступлении новой порции отсчётов
    void setSamplesCallback(std::function<void(const std::vector<int16_t>&)> cb);

    // Колбэк ошибок (опционально)
    void setErrorCallback(std::function<void(const std::string&)> cb);

private:
    void readerLoop();
    bool configureAndStart();
    void cleanup();
    void notifyError(const std::string& msg);

    std::thread readerThread_;
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> stopRequested_{false};

    std::function<void(const std::vector<int16_t>&)> onSamples_;
    std::function<void(const std::string&)> onError_;

    ILE2010* pModule_ = nullptr;
    HANDLE hStopEvent_ = nullptr;

    int channel_ = 0;
    double sampleRateKHz_ = 10000.0;
    DWORD dataStep_ = 1024 * 1024;

    std::vector<int16_t> buffer_[2];
    OVERLAPPED overlap_[2];
    IO_REQUEST_LUSBAPI ioReq_[2];
    WORD requestNumber_ = 0;
    ADC_PARS_E2010 adcParams_{};

    HANDLE hModuleHandle_ = nullptr;
    MODULE_DESCRIPTION_E2010 moduleDesc_{};
    bool calibrationLoaded_ = false;
};

} // namespace orbita
