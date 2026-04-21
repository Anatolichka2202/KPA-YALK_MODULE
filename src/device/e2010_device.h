// e2010_device.h
#pragma once

#include <QObject>
#include <thread>
#include <vector>
#include <windows.h>
#include <cstdint>
#include <atomic>
#include "../decoder/bitstream_recoverer.h"
#include "Lusbapi.h"

namespace orbita {

class E2010Device : public QObject
{
    Q_OBJECT
public:
    explicit E2010Device(QObject *parent = nullptr);
    ~E2010Device();

    bool init(int slot = 0, int channel = 0, double sampleRateKHz = 10000.0);
    bool start();
    void stop();

    bool isRunning() const { return isRunning_.loadRelaxed(); }

    void setRecoverer(BitstreamRecoverer* recoverer);

signals:
    void samplesReady(const std::vector<int16_t>& samples);
    void error(const QString& message);

private:
    void readerLoop();
    bool configureAndStart();
    void cleanup();

   std::thread readerThread_;
    QAtomicInt isRunning_{0};
    std::atomic<bool> stopRequested_{false};

    ILE2010* pModule_ = nullptr; // Указатель на интерфейс из Lusbapi
    HANDLE hStopEvent_ = nullptr; // Событие для пробуждения потока при остановке

    // Параметры сбора
    int channel_ = 0;
    double sampleRateKHz_ = 10000.0; //10Мгц
    DWORD dataStep_ = 1024 * 1024; // Размер одного буфера в отсчётах

    // Двойной буфер
    std::vector<int16_t> buffer_[2];
    OVERLAPPED overlap_[2];
    IO_REQUEST_LUSBAPI ioReq_[2];
    WORD requestNumber_ = 0; // 0 или 1, как в Delphi
    ADC_PARS_E2010 adcParams_;   // Параметры АЦП

    HANDLE hModuleHandle_ = nullptr;

    MODULE_DESCRIPTION_E2010 moduleDesc_;
    bool calibrationLoaded_;

    BitstreamRecoverer* recoverer_ = nullptr;
    void setupRecovererConnections();
};

} // namespace orbita
