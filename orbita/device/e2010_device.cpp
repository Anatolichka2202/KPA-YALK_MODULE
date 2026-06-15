#include "e2010_device.h"
#include "../orbita_core/logger.h"
#include <windows.h>

typedef DWORD (WINAPI *GetDllVersion_t)(void);
typedef ILE2010* (WINAPI *CreateLInstance_t)(const char*);

static GetDllVersion_t  pGetDllVersion  = nullptr;
static CreateLInstance_t pCreateLInstance = nullptr;
static HMODULE hLusbapiDll = nullptr;

namespace orbita {

E2010Device::~E2010Device() {
    stop();
    if (readerThread_.joinable()) readerThread_.join();
    if (hLusbapiDll) {
        FreeLibrary(hLusbapiDll);
        hLusbapiDll = nullptr;
    }
}

void E2010Device::setSamplesCallback(std::function<void(const std::vector<int16_t>&)> cb) {
    onSamples_ = std::move(cb);
}

void E2010Device::setErrorCallback(std::function<void(const std::string&)> cb) {
    onError_ = std::move(cb);
}

void E2010Device::notifyError(const std::string& msg) {
    LOG_ERROR("%s", msg.c_str());
    if (onError_) onError_(msg);
}

bool E2010Device::init(int slot, int channel, double sampleRateKHz) {
    channel_ = channel;
    sampleRateKHz_ = sampleRateKHz;

    if (channel < 0 || channel >= 4) {
        LOG_ERROR("E2010: invalid channel %d", channel);
        return false;
    }

    hLusbapiDll = LoadLibraryA("Lusbapi64.dll");
    if (!hLusbapiDll) {
        LOG_ERROR("E2010: failed to load Lusbapi64.dll");
        return false;
    }

    pGetDllVersion  = (GetDllVersion_t) GetProcAddress(hLusbapiDll, "GetDllVersion");
    pCreateLInstance = (CreateLInstance_t)GetProcAddress(hLusbapiDll, "CreateLInstance");
    if (!pGetDllVersion || !pCreateLInstance) {
        LOG_ERROR("E2010: failed to get Lusbapi functions");
        return false;
    }

    if (pGetDllVersion() != CURRENT_VERSION_LUSBAPI) {
        LOG_ERROR("E2010: incorrect Lusbapi.dll version");
        return false;
    }

    pModule_ = pCreateLInstance("e2010");
    if (!pModule_) {
        LOG_ERROR("E2010: CreateLInstance failed");
        return false;
    }

    if (!pModule_->OpenLDevice(slot)) {
        LOG_ERROR("E2010: OpenLDevice failed (slot=%d)", slot);
        return false;
    }

    hModuleHandle_ = pModule_->GetModuleHandle();
    if (!hModuleHandle_) {
        LOG_ERROR("E2010: GetModuleHandle failed");
        return false;
    }

    if (!pModule_->LOAD_MODULE(nullptr)) {
        LOG_ERROR("E2010: LOAD_MODULE failed");
        return false;
    }

    ADC_PARS_E2010 ap = {};
    pModule_->GET_ADC_PARS(&ap);

    if (!pModule_->GET_MODULE_DESCRIPTION(&moduleDesc_)) {
        LOG_ERROR("E2010: GET_MODULE_DESCRIPTION failed");
        return false;
    }
    calibrationLoaded_ = true;

    for (int range = 0; range < ADC_INPUT_RANGES_QUANTITY_E2010; ++range) {
        for (int ch = 0; ch < ADC_CHANNELS_QUANTITY_E2010; ++ch) {
            int idx = ch + range * ADC_CHANNELS_QUANTITY_E2010;
            ap.AdcOffsetCoefs[range][ch] = moduleDesc_.Adc.OffsetCalibration[idx];
            ap.AdcScaleCoefs[range][ch]  = moduleDesc_.Adc.ScaleCalibration[idx];
        }
    }

    ap.IsAdcCorrectionEnabled        = TRUE;
    ap.SynchroPars.StartSource       = INT_ADC_START_E2010;
    ap.SynchroPars.SynhroSource      = INT_ADC_CLOCK_E2010;
    ap.OverloadMode                  = CLIPPING_OVERLOAD_E2010;
    ap.ChannelsQuantity              = 1;
    ap.ControlTable[0]               = channel_;
    ap.AdcRate                       = sampleRateKHz_;
    ap.InterKadrDelay                = 0.0;
    ap.InputRange[0]                 = ADC_INPUT_RANGE_3000mV_E2010;
    ap.InputSwitch[0]                = ADC_INPUT_SIGNAL_E2010;
    ap.SynchroPars.StartDelay        = 0;
    ap.SynchroPars.StopAfterNKadrs   = 0;
    ap.SynchroPars.SynchroAdMode     = NO_ANALOG_SYNCHRO_E2010;
    ap.SynchroPars.SynchroAdChannel  = 0;
    ap.SynchroPars.SynchroAdPorog    = 0;
    ap.SynchroPars.IsBlockDataMarkerEnabled = 0;

    if (!pModule_->SET_ADC_PARS(&ap)) {
        LOG_ERROR("E2010: SET_ADC_PARS failed");
        return false;
    }
    adcParams_ = ap;

    for (int i = 0; i < 2; ++i)
        buffer_[i].resize(dataStep_);

    pModule_->STOP_ADC();
    LOG_INFO("E2010: init OK (ch=%d, rate=%.1f kHz)", channel_, sampleRateKHz_);
    return true;
}

bool E2010Device::start() {
    if (isRunning_.exchange(true)) return false;
    if (!pModule_) {
        isRunning_ = false;
        return false;
    }

    hStopEvent_ = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    for (int i = 0; i < 2; ++i) {
        ZeroMemory(&overlap_[i], sizeof(OVERLAPPED));
        overlap_[i].hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        ioReq_[i].Buffer              = buffer_[i].data();
        ioReq_[i].NumberOfWordsToPass = dataStep_;
        ioReq_[i].NumberOfWordsPassed = 0;
        ioReq_[i].Overlapped          = &overlap_[i];
        ioReq_[i].TimeOut             = (DWORD)(dataStep_ / adcParams_.AdcRate) + 1000;
    }

    requestNumber_ = 0;
    if (!pModule_->ReadData(&ioReq_[requestNumber_])) {
        LOG_ERROR("E2010: initial ReadData failed");
        isRunning_ = false;
        return false;
    }

    if (!pModule_->START_ADC()) {
        LOG_ERROR("E2010: START_ADC failed");
        isRunning_ = false;
        return false;
    }

    stopRequested_ = false;
    readerThread_ = std::thread(&E2010Device::readerLoop, this);
    LOG_INFO("E2010: started");
    return true;
}

void E2010Device::stop() {
    if (!isRunning_.exchange(false)) return;
    stopRequested_ = true;
    if (hStopEvent_) SetEvent(hStopEvent_);
    if (readerThread_.joinable()) readerThread_.join();

    if (pModule_) {
        pModule_->STOP_ADC();
        pModule_->CloseLDevice();
        pModule_->ReleaseLInstance();
        pModule_ = nullptr;
    }
    for (int i = 0; i < 2; ++i) {
        if (overlap_[i].hEvent) CloseHandle(overlap_[i].hEvent);
        overlap_[i].hEvent = nullptr;
    }
    if (hStopEvent_) CloseHandle(hStopEvent_);
    hStopEvent_ = nullptr;
    LOG_INFO("E2010: stopped");
}

void E2010Device::readerLoop() {
    DWORD bytesTransferred = 0;

    while (!stopRequested_) {
        requestNumber_ ^= 1;

        if (!pModule_->ReadData(&ioReq_[requestNumber_])) {
            notifyError("E2010: ReadData failed");
            break;
        }

        HANDLE waitObjects[2] = {
            ioReq_[requestNumber_ ^ 1].Overlapped->hEvent,
            hStopEvent_
        };
        DWORD waitResult = WaitForMultipleObjects(2, waitObjects, FALSE,
                                                  ioReq_[requestNumber_ ^ 1].TimeOut);

        if (waitResult == WAIT_OBJECT_0) {
            if (GetOverlappedResult(hModuleHandle_,
                                    ioReq_[requestNumber_ ^ 1].Overlapped,
                                    &bytesTransferred, FALSE)) {
                size_t words = bytesTransferred / sizeof(int16_t);
                if (words > 0 && onSamples_) {
                    std::vector<int16_t> samples(
                        buffer_[requestNumber_ ^ 1].data(),
                        buffer_[requestNumber_ ^ 1].data() + words);
                    onSamples_(samples);
                }
            } else {
                notifyError("E2010: GetOverlappedResult failed, code="
                            + std::to_string(GetLastError()));
                break;
            }
        } else if (waitResult == WAIT_OBJECT_0 + 1) {
            break; // stop event
        } else if (waitResult == WAIT_TIMEOUT) {
            notifyError("E2010: data wait timeout");
        } else {
            notifyError("E2010: WaitForMultipleObjects failed");
            break;
        }
    }

    if (pModule_) pModule_->STOP_ADC();
    LOG_INFO("E2010: reader loop finished");
}

} // namespace orbita
