#pragma once
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QAtomicInt>
#include <vector>
#include <cstdint>
#include <windows.h>

class E2010Device : public QObject
{
    Q_OBJECT
public:
    explicit E2010Device(QObject *parent = nullptr);
    ~E2010Device();

    // Методы, используемые в orbita_core
    bool init();   // вызовет init(0, 10000.0)
    bool setParam(const char* param, double value);
    const char* getInfo() const;
    bool startStream();
    void stopStream();
    int readSamples(int16_t* buffer, int max_samples, int timeout_ms);

    // Оригинальные методы
    bool init(int slot, double sampleRateKHz);
    bool start();
    void stop();

signals:
    void samplesReady(const std::vector<int16_t>& samples);

private:
    void readerLoop();

    QThread workerThread;
    QAtomicInt isRunning;
    QMutex mutex;
    QWaitCondition cond;

    // Для блокирующего чтения (readSamples)
    QMutex bufferMutex;
    QWaitCondition bufferCond;
    std::vector<int16_t> pendingBuffer;
    bool dataReady = false;

    HMODULE lcompHandle = nullptr;
    void* device = nullptr;
    void* dataBuffer = nullptr;
    unsigned long* syncCounter = nullptr;
    unsigned long bufferSizeSamples = 0;
    unsigned long lastSync = 0;
    HANDLE hEvent = nullptr;

    std::atomic<bool> stop_requested{false};

    int channel = 0;
    double sampleRate = 10000.0;
    int inputRange = 3000;
};
