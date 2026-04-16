#include "e2010_device.h"
#include <windows.h>
#include "../include/ioctl.h"
#include "../include/e2010cmd.h"
#include "../include/ifc_ldev.h"
#include <cstring>

#include <atomic>
#include <algorithm>
#include <QDebug>
typedef IDaqLDevice* (__cdecl *CreateInstance_t)(ULONG Slot);

E2010Device::E2010Device(QObject *parent) : QObject(parent) {}

E2010Device::~E2010Device()
{
    stopStream();
    if (workerThread.isRunning()) {
        workerThread.quit();
        workerThread.wait();
    }
}

bool E2010Device::init() {
    return init(0, 10000.0);
}

bool E2010Device::setParam(const char* param, double value) {
    if (strcmp(param, "channel") == 0) {
        channel = static_cast<int>(value);
        return true;
    }
    if (strcmp(param, "sample_rate") == 0) {
        sampleRate = value;
        return true;
    }
    if (strcmp(param, "input_range") == 0) {
        inputRange = static_cast<int>(value);
        return true;
    }
    return false;
}

const char* E2010Device::getInfo() const {
    return "E20-10 (LCard)";
}

bool E2010Device::startStream() {
    start();
    return true;
}

void E2010Device::stopStream() {
    stop();
}

int E2010Device::readSamples(int16_t* buffer, int max_samples, int timeout_ms)
{
    QMutexLocker lock(&bufferMutex);
    // Ждём данные с таймаутом
    while (!dataReady && !stop_requested) {
        if (!bufferCond.wait(&bufferMutex, timeout_ms)) {
            return 0; // таймаут
        }
    }
    if (!dataReady) return 0; // возможно, остановлено
    int n = std::min(max_samples, (int)pendingBuffer.size());
    memcpy(buffer, pendingBuffer.data(), n * sizeof(int16_t));
    pendingBuffer.clear();
    dataReady = false;
    return n;
}

bool E2010Device::init(int slot, double sampleRateKHz)
{
    lcompHandle = LoadLibraryA("lcomp64.dll");
    if (!lcompHandle) return false;

    auto create = (CreateInstance_t)GetProcAddress(lcompHandle, "CreateInstance");
    if (!create) {
        FreeLibrary(lcompHandle);
        lcompHandle = nullptr;
        return false;
    }
    IDaqLDevice* dev = create(slot);
    if (!dev) return false;

    HANDLE h = dev->OpenLDevice();
    if (h == INVALID_HANDLE_VALUE) {
        dev->Release();
        return false;
    }

    char bios[] = "e2010";
    if (dev->LoadBios(bios) != 0) {
        dev->CloseLDevice();
        dev->Release();
        return false;
    }

    device = dev;

    ADC_PAR adcParams{};
    adcParams.t2.s_Type = L_ADC_PARAM;
    adcParams.t2.AutoInit = 1;
    adcParams.t2.dRate = sampleRateKHz;
    adcParams.t2.dKadr = 0.00;   // частота кадра в кГц
    adcParams.t2.SynchroType = 0x01;
    adcParams.t2.SynchroSrc = 0x00;
    adcParams.t2.AdcIMask = (channel == 0) ? 0x0400 : 0x0200;
    adcParams.t2.NCh = 1;
    adcParams.t2.Chn[0] = channel;
    adcParams.t2.Chn[1] = 1;
    adcParams.t2.AdcEna = 1;
    adcParams.t2.IrqEna = 1;
    adcParams.t2.Pages = 32;
    adcParams.t2.IrqStep = 32768;
    adcParams.t2.FIFO = 32768;

    if (dev->FillDAQparameters((PDAQ_PAR)&adcParams.t2) != 0) return false;

    bufferSizeSamples = adcParams.t2.Pages * adcParams.t2.IrqStep;
    ULONG reqSize = bufferSizeSamples;
    if (dev->RequestBufferStream(&reqSize, L_STREAM_ADC) != 0) return false;
    bufferSizeSamples = reqSize;

    ULONG usedSize = 0;
    if (dev->SetParametersStream((PDAQ_PAR)&adcParams.t2, &usedSize,
                                 &dataBuffer, (void**)&syncCounter, L_STREAM_ADC) != 0)
        return false;

    hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!hEvent) return false;
    dev->SetLDeviceEvent(hEvent, L_STREAM_ADC);

    lastSync = 0;
    return true;
}

bool E2010Device::start()
{
    if (isRunning.fetchAndStoreRelaxed(1)) return false;
    if (!device) return false;

    IDaqLDevice* dev = static_cast<IDaqLDevice*>(device);
    if (dev->InitStartLDevice() != 0 || dev->StartLDevice() != 0) {
        isRunning = 0;
        return false;
    }

    workerThread.quit();
    workerThread.wait();
    moveToThread(&workerThread);
    connect(&workerThread, &QThread::started, this, &E2010Device::readerLoop);
    workerThread.start();
    return true;
}

void E2010Device::stop()
{
    if (!isRunning.fetchAndStoreRelaxed(0)) return;
    stop_requested = true;
    cond.wakeAll();
    bufferCond.wakeAll(); // разбудить readSamples
    if (workerThread.isRunning()) {
        workerThread.quit();
        workerThread.wait();
    }
    if (device) {
        IDaqLDevice* dev = static_cast<IDaqLDevice*>(device);
        dev->StopLDevice();
        dev->CloseLDevice();
        dev->Release();
        device = nullptr;
    }
    if (hEvent) CloseHandle(hEvent);
    if (lcompHandle) FreeLibrary(lcompHandle);
}

void E2010Device::readerLoop()
{
    qDebug() << "Reader loop started (polling mode)";
    while (isRunning.loadRelaxed() && !stop_requested) {
        ULONG cur = *syncCounter;
        ULONG diff = (cur >= lastSync) ? (cur - lastSync) : (0xFFFFFFFF - lastSync + cur + 1);
        if (diff > 0 && diff <= bufferSizeSamples * 2) {
            ULONG start = lastSync % bufferSizeSamples;
            ULONG end = start + diff;
            int16_t* ptr = static_cast<int16_t*>(dataBuffer);
            std::vector<int16_t> newData(diff);
            if (end <= bufferSizeSamples) {
                std::copy(ptr + start, ptr + end, newData.begin());
            } else {
                size_t first = bufferSizeSamples - start;
                std::copy(ptr + start, ptr + bufferSizeSamples, newData.begin());
                std::copy(ptr, ptr + (end - bufferSizeSamples), newData.begin() + first);
            }
            lastSync = cur;
            qDebug() << "Got" << diff << "samples, total counter" << cur;

            emit samplesReady(newData);

            {
                QMutexLocker lock(&bufferMutex);
                pendingBuffer = std::move(newData);
                dataReady = true;
                bufferCond.wakeOne();
            }
        }
        // Небольшая задержка, чтобы не грузить процессор
        QThread::msleep(10);
    }
    qDebug() << "Reader loop finished";
}
