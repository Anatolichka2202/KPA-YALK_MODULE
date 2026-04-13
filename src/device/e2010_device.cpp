#include "e2010_device.h"
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>

// Структура для асинхронного запроса
struct IO_REQUEST_LUSBAPI {
    short* Buffer;
    DWORD NumberOfWordsToPass;
    DWORD NumberOfWordsPassed;
    OVERLAPPED* Overlapped;
    DWORD TimeOut;
};

E2010Device::E2010Device()
    : lusbapi_handle(nullptr), pModule(nullptr), active_channel(0),
    sample_rate_khz(10000.0), input_range_mv(3000),
    current_buffer(0), read_thread(nullptr), is_streaming(false), stop_requested(false)
{
    info_string = "E20-10 (Lcard) - not initialized";
}

E2010Device::~E2010Device() {
    if (is_streaming) stopStream();
    unloadLibrary();
}

bool E2010Device::loadLibrary() {
    // Динамическая загрузка Lusbapi.dll
    lusbapi_handle = LoadLibraryA("Lusbapi.dll");
    if (!lusbapi_handle) return false;

    // Получаем функции
    CreateLInstance_t createLInstance = (CreateLInstance_t)GetProcAddress(lusbapi_handle, "CreateLInstance");
    if (!createLInstance) return false;

    pModule = (ILE2010*)createLInstance("e2010");
    if (!pModule) return false;

    return true;
}

bool E2010Device::configureADC() {
    if (!pModule) return false;

    // Получаем текущие параметры
    ADC_PARS_E2010 ap;
    if (!pModule->GET_ADC_PARS(&ap)) return false;

    // Настройка каналов (используем один канал)
    ap.ChannelsQuantity = 1;
    ap.ControlTable[0] = active_channel;

    // Настройка входного диапазона
    DWORD range_code;
    if (input_range_mv == 3000) range_code = 0;      // ADC_INPUT_RANGE_3000mV_E2010
    else if (input_range_mv == 1000) range_code = 1; // ADC_INPUT_RANGE_1000mV_E2010
    else range_code = 2;                              // ADC_INPUT_RANGE_300mV_E2010

    for (int i = 0; i < 4; i++) {
        ap.InputRange[i] = range_code;
        ap.InputSwitch[i] = 1; // ADC_INPUT_SIGNAL_E2010
    }

    // Частота дискретизации в кГц
    ap.AdcRate = sample_rate_khz;

    // Настройка синхронизации (внутренний старт)
    ap.SynchroPars.StartSource = 0;      // INT_ADC_START_E2010
    ap.SynchroPars.SynhroSource = 0;     // INT_ADC_CLOCK_E2010
    ap.SynchroPars.StartDelay = 0;
    ap.SynchroPars.StopAfterNKadrs = 0;
    ap.SynchroPars.SynchroAdMode = 0;    // NO_ANALOG_SYNCHRO_E2010
    ap.SynchroPars.SynchroAdChannel = 0;
    ap.SynchroPars.SynchroAdPorog = 0;
    ap.SynchroPars.IsBlockDataMarkerEnabled = 0;

    // Режим перегрузки
    ap.OverloadMode = 0;                 // CLIPPING_OVERLOAD_E2010

    // Устанавливаем параметры
    if (!pModule->SET_ADC_PARS(&ap)) return false;

    return true;
}

bool E2010Device::init() {
    if (!loadLibrary()) {
        info_string = "Failed to load Lusbapi.dll";
        return false;
    }

    // Открытие устройства в виртуальном слоте 0
    if (!pModule->OpenLDevice(0)) {
        info_string = "Failed to open device in virtual slot 0";
        return false;
    }

    // Получаем скорость USB
    BYTE usb_speed;
    if (!pModule->GetUsbSpeed(&usb_speed)) {
        info_string = "Failed to get USB speed";
        return false;
    }

    // Настройка АЦП
    if (!configureADC()) {
        info_string = "Failed to configure ADC";
        return false;
    }

    // Загрузка ПЛИС
    if (!pModule->LOAD_MODULE(nullptr)) {
        info_string = "Failed to load FPGA bitstream";
        return false;
    }

    // Тестирование модуля
    if (!pModule->TEST_MODULE(0)) {
        info_string = "Module test failed";
        return false;
    }

    // Получаем название модуля
    char module_name[256];
    if (pModule->GetModuleName(module_name)) {
        info_string = "E20-10 (Lcard) - ";
        info_string += module_name;
    } else {
        info_string = "E20-10 (Lcard) - initialized";
    }

    return true;
}

DWORD WINAPI E2010Device::readThreadProc(LPVOID param) {
    E2010Device* self = (E2010Device*)param;
    if (!self) return 1;

    // Настройка асинхронных буферов (двойная буферизация)
    const int BUFFER_SIZE = 1024 * 1024; // 1 млн отсчётов
    self->buffer_a.resize(BUFFER_SIZE);
    self->buffer_b.resize(BUFFER_SIZE);
    self->overlapped.resize(2);

    // Создаём события для асинхронных операций
    self->overlapped[0].hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    self->overlapped[1].hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!self->overlapped[0].hEvent || !self->overlapped[1].hEvent) return 1;

    // Запрашиваем первый буфер
    IO_REQUEST_LUSBAPI io_req;
    io_req.Buffer = self->buffer_a.data();
    io_req.NumberOfWordsToPass = BUFFER_SIZE;
    io_req.NumberOfWordsPassed = 0;
    io_req.Overlapped = &self->overlapped[0];
    io_req.TimeOut = 1000;

    if (!self->pModule->ReadData(&io_req)) {
        return 1;
    }

    // Запускаем АЦП
    if (!self->pModule->START_ADC()) {
        return 1;
    }

    self->is_streaming = true;
    int current = 0;

    // Основной цикл сбора данных
    while (!self->stop_requested) {
        int next = current ^ 1; // переключение буфера

        // Запрашиваем следующий буфер
        io_req.Buffer = (next == 0) ? self->buffer_a.data() : self->buffer_b.data();
        io_req.Overlapped = &self->overlapped[next];
        self->pModule->ReadData(&io_req);

        // Ожидаем завершения текущего буфера
        DWORD wait_result = WaitForSingleObject(self->overlapped[current].hEvent, 1000);

        if (wait_result == WAIT_OBJECT_0) {
            // Буфер готов, можно обрабатывать
            // Данные уже лежат в buffer_a или buffer_b
            // Здесь вызывается callback для передачи данных
        }

        current = next;
    }

    // Остановка АЦП
    self->pModule->STOP_ADC();

    // Закрываем события
    CloseHandle(self->overlapped[0].hEvent);
    CloseHandle(self->overlapped[1].hEvent);

    self->is_streaming = false;
    return 0;
}

bool E2010Device::startStream() {
    if (!pModule) return false;
    if (is_streaming) return true;

    stop_requested = false;
    read_thread = CreateThread(NULL, 0, readThreadProc, this, 0, &thread_id);
    if (!read_thread) return false;

    return true;
}

bool E2010Device::stopStream() {
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

int E2010Device::readSamples(int16_t* buffer, int max_samples, int timeout_ms) {
    // Чтение из текущего готового буфера
    // В реальной реализации нужно синхронизироваться с потоком
    // и копировать данные из buffer_a или buffer_b

    // Пока заглушка
    return 0;
}

bool E2010Device::setParam(const char* param, double value) {
    if (strcmp(param, "sample_rate") == 0) {
        sample_rate_khz = value;
        if (pModule) configureADC();
        return true;
    } else if (strcmp(param, "channel") == 0) {
        active_channel = (int)value;
        if (pModule) configureADC();
        return true;
    } else if (strcmp(param, "input_range") == 0) {
        input_range_mv = (int)value;
        if (pModule) configureADC();
        return true;
    }
    return false;
}

const char* E2010Device::getInfo() const {
    return info_string.c_str();
}

void E2010Device::unloadLibrary() {
    if (pModule) {
        pModule->ReleaseLInstance();
        pModule = nullptr;
    }
    if (lusbapi_handle) {
        FreeLibrary(lusbapi_handle);
        lusbapi_handle = nullptr;
    }
}
