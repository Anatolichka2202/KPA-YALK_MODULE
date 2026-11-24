#pragma once

#include "orbita_stand/sample_source.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include "Lusbapi.h"
#endif

namespace orbita::stand {

class E2010SampleSource final : public ISampleSource {
public:
    explicit E2010SampleSource(std::map<std::string, std::string> configuration);
    ~E2010SampleSource() override;

    E2010SampleSource(const E2010SampleSource&) = delete;
    E2010SampleSource& operator=(const E2010SampleSource&) = delete;

    bool open() override;
    void close() noexcept override;
    bool isOpen() const noexcept override { return open_.load(); }

    bool start() override;
    void stop() noexcept override;
    bool isRunning() const noexcept override { return running_.load(); }

    void setSamplesCallback(SamplesCallback callback) override;
    void setErrorCallback(ErrorCallback callback) override;

private:
    void notifyError(const std::string& message) noexcept;
    void readerLoop();

#ifdef _WIN32
    using GetDllVersionFn = DWORD (WINAPI *)(void);
    using CreateInstanceFn = ILE2010* (WINAPI *)(const char*);

    bool configureHardware();
    void releaseHardware() noexcept;
    void closeIoEvents() noexcept;

    HMODULE lusbapiDll_ = nullptr;
    GetDllVersionFn getDllVersion_ = nullptr;
    CreateInstanceFn createInstance_ = nullptr;
    ILE2010* module_ = nullptr;
    HANDLE moduleHandle_ = nullptr;
    HANDLE stopEvent_ = nullptr;

    std::vector<int16_t> buffers_[2];
    OVERLAPPED overlap_[2]{};
    IO_REQUEST_LUSBAPI requests_[2]{};
    WORD requestNumber_ = 0;
    ADC_PARS_E2010 adcParams_{};
    MODULE_DESCRIPTION_E2010 moduleDescription_{};
#endif

    std::map<std::string, std::string> configuration_;
    int slot_ = 0;
    int channel_ = 0;
    double sampleRateKHz_ = 10000.0;
    std::size_t bufferWords_ = 1024u * 1024u;

    std::atomic_bool open_{false};
    std::atomic_bool running_{false};
    std::atomic_bool stopRequested_{false};
    std::thread readerThread_;

    SamplesCallback samplesCallback_;
    ErrorCallback errorCallback_;
};

} // namespace orbita::stand
