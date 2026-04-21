// тест dll и библиотек
#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <windows.h>
#include <vector>
#include <cstdint>
#include <atomic>
#include <thread>

#include "Lusbapi.h"

typedef DWORD (WINAPI *GetDllVersion_t)(void);
typedef ILE2010* (WINAPI *CreateLInstance_t)(const char*);

static HMODULE hDll = nullptr;
static GetDllVersion_t pGetDllVersion = nullptr;
static CreateLInstance_t pCreateLInstance = nullptr;

bool loadLusbapiDll() {
    QString dllPath = QCoreApplication::applicationDirPath() + "/Lusbapi64.dll";
    hDll = LoadLibraryA(dllPath.toStdString().c_str());
    if (!hDll) {
        qCritical() << "Failed to load Lusbapi.dll from" << dllPath;
        return false;
    }
    pGetDllVersion = (GetDllVersion_t)GetProcAddress(hDll, "GetDllVersion");
    pCreateLInstance = (CreateLInstance_t)GetProcAddress(hDll, "CreateLInstance");
    if (!pGetDllVersion || !pCreateLInstance) {
        qCritical() << "Failed to get functions";
        return false;
    }
    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (!loadLusbapiDll()) return 1;
    qDebug() << "Lusbapi.dll loaded, version:" << pGetDllVersion();

    ILE2010* pModule = pCreateLInstance("e2010");
    if (!pModule) {
        qCritical() << "CreateLInstance failed";
        return 1;
    }
    qDebug() << "ILE2010 instance created";

    if (!pModule->OpenLDevice(0)) {
        LAST_ERROR_INFO_LUSBAPI err;
        if (pModule->GetLastErrorInfo(&err))
            qCritical() << "OpenLDevice failed:" << (char*)err.ErrorString;
        else
            qCritical() << "OpenLDevice failed";
        return 1;
    }
    qDebug() << "OpenLDevice succeeded on slot 0";

    HANDLE hModule = pModule->GetModuleHandle();
    if (!hModule) {
        qCritical() << "GetModuleHandle failed";
        return 1;
    }

    if (!pModule->LOAD_MODULE(nullptr)) {
        qCritical() << "LOAD_MODULE failed";
        return 1;
    }
    qDebug() << "LOAD_MODULE succeeded";

    char moduleName[256] = {0};
    if (pModule->GetModuleName(moduleName))
        qDebug() << "Module name:" << moduleName;

    // Настройка АЦП (канал 0)
    ADC_PARS_E2010 ap;
    pModule->GET_ADC_PARS(&ap);
    ap.IsAdcCorrectionEnabled = TRUE;
    ap.SynchroPars.StartSource = INT_ADC_START_E2010;
    ap.SynchroPars.SynhroSource = INT_ADC_CLOCK_E2010;
    ap.OverloadMode = CLIPPING_OVERLOAD_E2010;
    ap.ChannelsQuantity = 1;
    ap.ControlTable[0] = 0;
    ap.AdcRate = 10000.0;
    ap.InterKadrDelay = 0.0;
    ap.InputRange[0] = ADC_INPUT_RANGE_3000mV_E2010;
    ap.InputSwitch[0] = ADC_INPUT_SIGNAL_E2010;

    if (!pModule->SET_ADC_PARS(&ap)) {
        qCritical() << "SET_ADC_PARS failed";
        return 1;
    }
    qDebug() << "ADC parameters set (10 MHz, channel 0)";

    const DWORD dataStep = 1024 * 1024; // 1 млн отсчётов
    std::vector<int16_t> buffer[2];
    buffer[0].resize(dataStep);
    buffer[1].resize(dataStep);
    OVERLAPPED overlap[2] = {};
    IO_REQUEST_LUSBAPI ioReq[2] = {};

    // Инициализация структур
    for (int i = 0; i < 2; ++i) {
        ZeroMemory(&overlap[i], sizeof(OVERLAPPED));
        overlap[i].hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        ioReq[i].Buffer = buffer[i].data();
        ioReq[i].NumberOfWordsToPass = dataStep;
        ioReq[i].NumberOfWordsPassed = 0;
        ioReq[i].Overlapped = &overlap[i];
        ioReq[i].TimeOut = (DWORD)(dataStep / ap.AdcRate) + 1000;
    }

    // Остановка АЦП для очистки состояния
    pModule->STOP_ADC();

    // Запускаем оба асинхронных запроса ДО старта АЦП (как в Delphi)
    if (!pModule->ReadData(&ioReq[0])) {
        qCritical() << "Initial ReadData[0] failed";
        return 1;
    }
    if (!pModule->ReadData(&ioReq[1])) {
        qCritical() << "Initial ReadData[1] failed";
        return 1;
    }

    // Стартуем АЦП
    if (!pModule->START_ADC()) {
        qCritical() << "START_ADC failed";
        return 1;
    }
    qDebug() << "ADC started with double async requests, waiting...";

    std::atomic<bool> running{true};
    std::atomic<int> totalSamples{0};

    // Поток сбора данных (точно как ReadThread в Delphi)
    std::thread readerThread([&]() {
        int reqNum = 0; // 0 или 1
        while (running) {
            reqNum ^= 1; // переключаем 0->1->0...

            // Запускаем новый запрос в буфер reqNum
            if (!pModule->ReadData(&ioReq[reqNum])) {
                qCritical() << "ReadData failed for buffer" << reqNum;
                running = false;
                break;
            }

            // Ждём завершения ПРЕДЫДУЩЕГО запроса (reqNum ^ 1)
            HANDLE h = ioReq[reqNum ^ 1].Overlapped->hEvent;
            DWORD waitRes = WaitForSingleObject(h, ioReq[reqNum ^ 1].TimeOut);

            if (waitRes == WAIT_OBJECT_0) {
                DWORD bytesTransferred = 0;
                if (GetOverlappedResult(hModule, ioReq[reqNum ^ 1].Overlapped, &bytesTransferred, FALSE)) {
                    size_t wordsTransferred = bytesTransferred / sizeof(int16_t);
                    if (wordsTransferred > 0) {
                        totalSamples += wordsTransferred;
                        qDebug() << "Received" << wordsTransferred << "samples, total" << totalSamples;
                    } else {
                        qDebug() << "GetOverlappedResult returned 0 bytes for buffer" << (reqNum ^ 1);
                    }
                } else {
                    DWORD err = GetLastError();
                    qCritical() << "GetOverlappedResult failed, error code:" << err;
                    running = false;
                    break;
                }

                // Проверка переполнения (оставьте как было)
                DATA_STATE_E2010 state;
                if (pModule->GET_DATA_STATE(&state)) {
                    if (state.BufferOverrun & 1) {
                        qWarning() << "Buffer overrun detected!";
                    }
                }
            } else if (waitRes == WAIT_TIMEOUT) {
                qDebug() << "Timeout on buffer" << (reqNum ^ 1);
            } else {
                qCritical() << "Wait failed on buffer" << (reqNum ^ 1);
                running = false;
                break;
            }
        }
    });

    // Таймер для вывода состояния буфера
    QTimer stateTimer;
    QObject::connect(&stateTimer, &QTimer::timeout, [&]() {
        DATA_STATE_E2010 state;
        if (pModule->GET_DATA_STATE(&state)) {
            qDebug() << "Buffer fill:" << state.CurBufferFilling << "of" << state.BufferSize
                     << "(" << state.CurBufferFillingPercent << "%)"
                     << "Overrun:" << (state.BufferOverrun & 1);
        }
    });
    stateTimer.start(1000);

    // Остановка через 10 секунд
    QTimer::singleShot(10000, [&]() {
        qDebug() << "Stopping...";
        running = false;
        // Разбудить ожидающие потоки
        for (int i = 0; i < 2; ++i) SetEvent(overlap[i].hEvent);
        QCoreApplication::quit();
    });

    app.exec();

    running = false;
    readerThread.join();
    pModule->STOP_ADC();
    pModule->CloseLDevice();
    pModule->ReleaseLInstance();
    FreeLibrary(hDll);

    for (int i = 0; i < 2; ++i) CloseHandle(overlap[i].hEvent);

    qDebug() << "Total samples received:" << totalSamples.load();
    return 0;
}
