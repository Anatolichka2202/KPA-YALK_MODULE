#include "e2010_device.h"
#include <QDebug>
#include <windows.h>

// Типы указателей на функции из Lusbapi.dll
typedef DWORD (WINAPI *GetDllVersion_t)(void);
typedef ILE2010* (WINAPI *CreateLInstance_t)(const char*);

// Глобальные указатели (можно сделать статическими членами)
static GetDllVersion_t pGetDllVersion = nullptr;
static CreateLInstance_t pCreateLInstance = nullptr;
static HMODULE hLusbapiDll = nullptr;

using namespace orbita;

E2010Device::E2010Device(QObject *parent) : QObject(parent) {}

E2010Device::~E2010Device() {
    stop();
    if (readerThread_.joinable()) readerThread_.join();
    if (hLusbapiDll) {
        FreeLibrary(hLusbapiDll);
        hLusbapiDll = nullptr;
    }
}

bool E2010Device::init(int slot, int channel, double sampleRateKHz) {

    channel_ = channel;
    sampleRateKHz_ = sampleRateKHz;

    if (channel < 0 || channel >= 4) {  // ADC_CHANNELS_QUANTITY_E2010 = 4
        qCritical() << "Invalid channel number:" << channel;
        return false;
    }

    // 1. Загружаем Lusbapi.dll
    hLusbapiDll = LoadLibraryA("Lusbapi64.dll");
    if (!hLusbapiDll) {
        qCritical() << "Failed to load Lusbapi.dll";
        return false;
    }

    // 2. Получаем функции
    pGetDllVersion = (GetDllVersion_t)GetProcAddress(hLusbapiDll, "GetDllVersion");
    pCreateLInstance = (CreateLInstance_t)GetProcAddress(hLusbapiDll, "CreateLInstance");
    if (!pGetDllVersion || !pCreateLInstance) {
        qCritical() << "Failed to get Lusbapi functions";
        return false;
    }

    // 3. Проверка версии
    if (pGetDllVersion() != CURRENT_VERSION_LUSBAPI) {
        qCritical() << "Incorrect Lusbapi.dll version";
        return false;
    }

    // 4. Создаём экземпляр интерфейса E2010
    pModule_ = pCreateLInstance("e2010");
    if (!pModule_) {
        qCritical() << "CreateLInstance failed";
        return false;
    }

    // 5. Открываем устройство
    if (!pModule_->OpenLDevice(slot)) {
        qCritical() << "OpenLDevice failed";
        return false;
    }

    hModuleHandle_ = pModule_->GetModuleHandle();
    if (!hModuleHandle_) {
        qCritical() << "GetModuleHandle failed";
        return false;
    }

    // 6. Загружаем прошивку ПЛИС
    if (!pModule_->LOAD_MODULE(nullptr)) {
        qCritical() << "LOAD_MODULE failed";
        return false;
    }

    // 7. Настройка параметров АЦП
    ADC_PARS_E2010 ap = {};
    pModule_->GET_ADC_PARS(&ap);

    //=======================калибровочные коэфы============
    if (!pModule_->GET_MODULE_DESCRIPTION(&moduleDesc_)) {
        qCritical() << "GET_MODULE_DESCRIPTION failed";
        return false;
    }
    calibrationLoaded_ = true;

    for (int range = 0; range < ADC_INPUT_RANGES_QUANTITY_E2010; ++range) {
        for (int ch = 0; ch < ADC_CHANNELS_QUANTITY_E2010; ++ch) {
            int idx = ch + range * ADC_CHANNELS_QUANTITY_E2010;
            ap.AdcOffsetCoefs[range][ch] = moduleDesc_.Adc.OffsetCalibration[idx];
            ap.AdcScaleCoefs[range][ch] = moduleDesc_.Adc.ScaleCalibration[idx];
        }
    }
    //======================================================

    ap.IsAdcCorrectionEnabled = TRUE;
    ap.SynchroPars.StartSource = INT_ADC_START_E2010;
    ap.SynchroPars.SynhroSource = INT_ADC_CLOCK_E2010;
    ap.OverloadMode = CLIPPING_OVERLOAD_E2010;
    ap.ChannelsQuantity = 1;
    ap.ControlTable[0] = channel_;
    ap.AdcRate = sampleRateKHz_;
    ap.InterKadrDelay = 0.0;
    ap.InputRange[0] = ADC_INPUT_RANGE_3000mV_E2010;
    ap.InputSwitch[0] = ADC_INPUT_SIGNAL_E2010;

    ap.SynchroPars.StartDelay = 0;
    ap.SynchroPars.StopAfterNKadrs = 0;
    ap.SynchroPars.SynchroAdMode = NO_ANALOG_SYNCHRO_E2010;
    ap.SynchroPars.SynchroAdChannel = 0;
    ap.SynchroPars.SynchroAdPorog = 0;
    ap.SynchroPars.IsBlockDataMarkerEnabled = 0;

    if (!pModule_->SET_ADC_PARS(&ap)) {
        qCritical() << "SET_ADC_PARS failed";
        return false;
    }
    adcParams_ = ap;

    // 8. Выделяем память под двойной буфер
    for (int i = 0; i < 2; ++i) {
        buffer_[i].resize(dataStep_);
    }


     pModule_->STOP_ADC();
    return true;
}

bool E2010Device::start() {
    if (isRunning_.fetchAndStoreRelaxed(1)) return false;
    if (!pModule_) return false;



    hStopEvent_ = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    for (int i = 0; i < 2; ++i) {
        ZeroMemory(&overlap_[i], sizeof(OVERLAPPED));
        overlap_[i].hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        ioReq_[i].Buffer = buffer_[i].data();
        ioReq_[i].NumberOfWordsToPass = dataStep_;
        ioReq_[i].NumberOfWordsPassed = 0;
        ioReq_[i].Overlapped = &overlap_[i];
        ioReq_[i].TimeOut = (DWORD)(dataStep_ / adcParams_.AdcRate) + 1000;
    }

    requestNumber_ = 0;
    if (!pModule_->ReadData(&ioReq_[requestNumber_])) {
        qCritical() << "Initial ReadData failed";
        return false;
    }

    if (!pModule_->START_ADC()) {
        qCritical() << "START_ADC failed";
        return false;
    }

    stopRequested_ = false;
    readerThread_ = std::thread(&E2010Device::readerLoop, this);



    return true;
}

void E2010Device::stop() {
    if (!isRunning_.fetchAndStoreRelaxed(0)) return;
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
}

void E2010Device::readerLoop() {
    HANDLE stopEvent = hStopEvent_;
    HANDLE hModule = hModuleHandle_;
    DWORD bytesTransferred = 0;

    while (!stopRequested_) {
        requestNumber_ ^= 1;

        if (!pModule_->ReadData(&ioReq_[requestNumber_])) {
            emit error("ReadData failed");
            break;
        }

        HANDLE waitObjects[2] = {
            ioReq_[requestNumber_ ^ 1].Overlapped->hEvent,
            stopEvent
        };
        DWORD waitResult = WaitForMultipleObjects(2, waitObjects, FALSE,
                                                  ioReq_[requestNumber_ ^ 1].TimeOut);

        if (waitResult == WAIT_OBJECT_0) {
            // Успешно дождались завершения предыдущего запроса
            if (GetOverlappedResult(hModule,
                                    ioReq_[requestNumber_ ^ 1].Overlapped,
                                    &bytesTransferred,
                                    FALSE)) {
                size_t wordsTransferred = bytesTransferred / sizeof(int16_t);
                if (wordsTransferred > 0) {
                    std::vector<int16_t> samples(
                        buffer_[requestNumber_ ^ 1].data(),
                        buffer_[requestNumber_ ^ 1].data() + wordsTransferred
                        );
                    emit samplesReady(samples);
                } else {
                    qDebug() << "GetOverlappedResult returned 0 bytes";
                }
            } else {
                DWORD err = GetLastError();
                emit error(QString("GetOverlappedResult failed, error code: %1").arg(err));
                break;
            }
        } else if (waitResult == WAIT_OBJECT_0 + 1) {
            break; // стоп-событие
        } else if (waitResult == WAIT_TIMEOUT) {
            emit error("Data wait timeout");
        } else {
            emit error("WaitForMultipleObjects failed");
            break;
        }
    }

    pModule_->STOP_ADC();
    qDebug() << "Reader loop finished";
}
void E2010Device::setRecoverer(BitstreamRecoverer* recoverer) {
    if (recoverer_ == recoverer) return;
    recoverer_ = recoverer;
    setupRecovererConnections();
}

void E2010Device::setupRecovererConnections() {
    if (!recoverer_) return;
    // Отключаем старые соединения
    disconnect(this, &E2010Device::samplesReady, nullptr, nullptr);
    // Подключаем новое
    connect(this, &E2010Device::samplesReady, this, [this](const std::vector<int16_t>& samples) {
        static bool thresholdSet = false;
        if (!thresholdSet && !samples.empty()) {
            size_t portion = samples.size() / 10;
            if (portion == 0) portion = samples.size();
            recoverer_->computeThreshold(samples.data(), samples.size(), portion);
            thresholdSet = true;
        }
        if (thresholdSet) {
            recoverer_->processSamples(samples.data(), samples.size());
        }
    });
}
