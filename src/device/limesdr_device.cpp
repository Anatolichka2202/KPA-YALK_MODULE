#include "limesdr_device.h"
#include <cstring>
#include <cstdio>

LimeSDRDevice::LimeSDRDevice()
    : limesuite_handle(nullptr), device(nullptr), rx_stream(nullptr),
    frequency_hz(2.2994e9), gain_db(40.0), sample_rate_hz(5e6),
    is_streaming(false), read_thread(nullptr), stop_requested(false)
{
    info_string = "LimeSDR - not initialized";
}

LimeSDRDevice::~LimeSDRDevice() {
    if (is_streaming) stopStream();
    unloadLibrary();
}

bool LimeSDRDevice::loadLibrary() {
    limesuite_handle = LoadLibraryA("LimeSuite.dll");
    if (!limesuite_handle) return false;

    // Получаем адреса функций
    LMS_Open_t LMS_Open = (LMS_Open_t)GetProcAddress(limesuite_handle, "LMS_Open");
    LMS_Init_t LMS_Init = (LMS_Init_t)GetProcAddress(limesuite_handle, "LMS_Init");
    LMS_SetupStream_t LMS_SetupStream = (LMS_SetupStream_t)GetProcAddress(limesuite_handle, "LMS_SetupStream");
    LMS_StartStream_t LMS_StartStream = (LMS_StartStream_t)GetProcAddress(limesuite_handle, "LMS_StartStream");
    LMS_RecvStream_t LMS_RecvStream = (LMS_RecvStream_t)GetProcAddress(limesuite_handle, "LMS_RecvStream");
    LMS_StopStream_t LMS_StopStream = (LMS_StopStream_t)GetProcAddress(limesuite_handle, "LMS_StopStream");
    LMS_DestroyStream_t LMS_DestroyStream = (LMS_DestroyStream_t)GetProcAddress(limesuite_handle, "LMS_DestroyStream");
    LMS_Close_t LMS_Close = (LMS_Close_t)GetProcAddress(limesuite_handle, "LMS_Close");
    LMS_EnableChannel_t LMS_EnableChannel = (LMS_EnableChannel_t)GetProcAddress(limesuite_handle, "LMS_EnableChannel");
    LMS_SetLOFrequency_t LMS_SetLOFrequency = (LMS_SetLOFrequency_t)GetProcAddress(limesuite_handle, "LMS_SetLOFrequency");
    LMS_SetGaindB_t LMS_SetGaindB = (LMS_SetGaindB_t)GetProcAddress(limesuite_handle, "LMS_SetGaindB");
    LMS_SetSampleRate_t LMS_SetSampleRate = (LMS_SetSampleRate_t)GetProcAddress(limesuite_handle, "LMS_SetSampleRate");

    if (!LMS_Open || !LMS_Init || !LMS_SetupStream || !LMS_StartStream || !LMS_RecvStream ||
        !LMS_StopStream || !LMS_DestroyStream || !LMS_Close || !LMS_EnableChannel ||
        !LMS_SetLOFrequency || !LMS_SetGaindB || !LMS_SetSampleRate) {
        return false;
    }

    // Открытие устройства
    if (LMS_Open(&device, NULL, NULL) != 0) return false;

    // Инициализация устройства
    if (LMS_Init(device) != 0) return false;

    // Установка параметров
    if (LMS_SetSampleRate(device, sample_rate_hz, 0) != 0) return false;
    if (LMS_SetLOFrequency(device, 0, 0, frequency_hz) != 0) return false;
    if (LMS_SetGaindB(device, 0, 0, gain_db) != 0) return false;

    // Включение RX канала
    if (LMS_EnableChannel(device, 0, 0, true) != 0) return false;

    // Настройка потока
    if (LMS_SetupStream(device, &rx_stream) != 0) return false;

    return true;
}

bool LimeSDRDevice::init() {
    if (!loadLibrary()) {
        info_string = "Failed to load LimeSuite.dll";
        return false;
    }
    info_string = "LimeSDR initialized successfully";
    return true;
}

DWORD WINAPI LimeSDRDevice::readThreadProc(LPVOID param) {
    LimeSDRDevice* self = (LimeSDRDevice*)param;
    if (!self) return 1;

    // Получаем функции
    LMS_RecvStream_t LMS_RecvStream = (LMS_RecvStream_t)GetProcAddress(self->limesuite_handle, "LMS_RecvStream");
    LMS_StopStream_t LMS_StopStream = (LMS_StopStream_t)GetProcAddress(self->limesuite_handle, "LMS_StopStream");

    if (!LMS_RecvStream || !LMS_StopStream) return 1;

    // Буфер для сэмплов (I/Q)
    const int BUF_SIZE = 1024 * 1024;
    std::vector<int16_t> buffer(BUF_SIZE * 2); // I и Q

    // Запуск потока
    LMS_StartStream_t LMS_StartStream = (LMS_StartStream_t)GetProcAddress(self->limesuite_handle, "LMS_StartStream");
    if (LMS_StartStream && LMS_StartStream(self->rx_stream) != 0) return 1;

    self->is_streaming = true;

    // Основной цикл приёма
    while (!self->stop_requested) {
        void* samples[] = { buffer.data() };
        int ret = LMS_RecvStream(self->rx_stream, samples, BUF_SIZE, NULL, 1000);
        if (ret > 0) {
            // Данные получены, можно обрабатывать
            // Здесь вызывается callback для передачи данных
        }
    }

    // Остановка потока
    LMS_StopStream(self->rx_stream);
    self->is_streaming = false;
    return 0;
}

bool LimeSDRDevice::startStream() {
    if (!device || !rx_stream) return false;
    if (is_streaming) return true;

    stop_requested = false;
    read_thread = CreateThread(NULL, 0, readThreadProc, this, 0, &thread_id);
    if (!read_thread) return false;

    return true;
}

bool LimeSDRDevice::stopStream() {
    if (!is_streaming) return true;

    stop_requested = true;
    if (read_thread) {
        WaitForSingleObject(read_thread, 5000);
        CloseHandle(read_thread);
        read_thread = nullptr;
    }

    is_streaming = false;
    return true;
}

int LimeSDRDevice::readSamples(int16_t* buffer, int max_samples, int timeout_ms) {
    // Чтение из внутреннего буфера
    // Пока заглушка
    return 0;
}

bool LimeSDRDevice::setParam(const char* param, double value) {
    if (strcmp(param, "frequency") == 0) {
        frequency_hz = value;
        return setFrequency(frequency_hz);
    } else if (strcmp(param, "gain") == 0) {
        gain_db = value;
        return setGain(gain_db);
    } else if (strcmp(param, "sample_rate") == 0) {
        sample_rate_hz = value;
        return setSampleRate(sample_rate_hz);
    }
    return false;
}

bool LimeSDRDevice::setFrequency(double freq_hz) {
    if (!device) return false;
    LMS_SetLOFrequency_t LMS_SetLOFrequency = (LMS_SetLOFrequency_t)GetProcAddress(limesuite_handle, "LMS_SetLOFrequency");
    if (!LMS_SetLOFrequency) return false;
    return LMS_SetLOFrequency(device, 0, 0, freq_hz) == 0;
}

bool LimeSDRDevice::setGain(double gain_db) {
    if (!device) return false;
    LMS_SetGaindB_t LMS_SetGaindB = (LMS_SetGaindB_t)GetProcAddress(limesuite_handle, "LMS_SetGaindB");
    if (!LMS_SetGaindB) return false;
    return LMS_SetGaindB(device, 0, 0, gain_db) == 0;
}

bool LimeSDRDevice::setSampleRate(double rate_hz) {
    if (!device) return false;
    LMS_SetSampleRate_t LMS_SetSampleRate = (LMS_SetSampleRate_t)GetProcAddress(limesuite_handle, "LMS_SetSampleRate");
    if (!LMS_SetSampleRate) return false;
    return LMS_SetSampleRate(device, rate_hz, 0) == 0;
}

const char* LimeSDRDevice::getInfo() const {
    return info_string.c_str();
}

void LimeSDRDevice::unloadLibrary() {
    if (rx_stream) {
        LMS_DestroyStream_t LMS_DestroyStream = (LMS_DestroyStream_t)GetProcAddress(limesuite_handle, "LMS_DestroyStream");
        if (LMS_DestroyStream) LMS_DestroyStream(device, rx_stream);
        rx_stream = nullptr;
    }
    if (device) {
        LMS_Close_t LMS_Close = (LMS_Close_t)GetProcAddress(limesuite_handle, "LMS_Close");
        if (LMS_Close) LMS_Close(device);
        device = nullptr;
    }
    if (limesuite_handle) {
        FreeLibrary(limesuite_handle);
        limesuite_handle = nullptr;
    }
}
