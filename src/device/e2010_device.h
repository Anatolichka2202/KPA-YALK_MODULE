/**
 * @file e2010_device.h
 * @brief Реализация интерфейса DeviceInterface для АЦП E20-10 (Lcard).
 *
 * Использует Lusbapi.dll (динамическая загрузка) для работы с модулем.
 */

#ifndef ORBITA_E2010_DEVICE_H
#define ORBITA_E2010_DEVICE_H

#include "device_interface.h"
#include <string>
#include <vector>
#include <windows.h>

// Предварительные объявления типов Lusbapi
typedef unsigned int DWORD;
typedef int BOOL;

// Структуры из Lusbapi (упрощённо)
typedef struct {
    DWORD StartSource;
    DWORD StartDelay;
    DWORD SynhroSource;
    DWORD StopAfterNKadrs;
    DWORD SynchroAdMode;
    DWORD SynchroAdChannel;
    short SynchroAdPorog;
    BYTE IsBlockDataMarkerEnabled;
} SYNCHRO_PARS_E2010;

typedef struct {
    BOOL IsAdcCorrectionEnabled;
    DWORD OverloadMode;
    DWORD InputCurrentControl;
    SYNCHRO_PARS_E2010 SynchroPars;
    DWORD ChannelsQuantity;
    DWORD ControlTable[256];
    DWORD InputRange[4];
    DWORD InputSwitch[4];
    double AdcRate;
    double InterKadrDelay;
    double KadrRate;
    double AdcOffsetCoefs[3][4];
    double AdcScaleCoefs[3][4];
} ADC_PARS_E2010;

// Интерфейс ILE2010 (чисто абстрактный класс)
struct ILE2010 {
    // Методы будут вызываться по смещению в vtable
    virtual BOOL OpenLDevice(DWORD VirtualSlot) = 0;
    virtual BOOL CloseLDevice() = 0;
    virtual BOOL ReleaseLInstance() = 0;
    virtual void* GetModuleHandle() = 0;
    virtual BOOL GetModuleName(char* ModuleName) = 0;
    virtual BOOL GetUsbSpeed(BYTE* UsbSpeed) = 0;
    virtual BOOL LowPowerMode(BOOL LowPowerFlag) = 0;
    virtual BOOL GetLastErrorInfo(void* LastErrorInfo) = 0;
    virtual BOOL LOAD_MODULE(void* FileName) = 0;
    virtual BOOL TEST_MODULE(DWORD TestModeMask = 0) = 0;
    virtual BOOL GET_ADC_PARS(ADC_PARS_E2010* ap) = 0;
    virtual BOOL SET_ADC_PARS(ADC_PARS_E2010* ap) = 0;
    virtual BOOL START_ADC() = 0;
    virtual BOOL STOP_ADC() = 0;
    virtual BOOL GET_DATA_STATE(void* DataState) = 0;
    virtual BOOL ReadData(void* ReadRequest) = 0;
    virtual BOOL DAC_SAMPLE(short* DacData, DWORD DacChannel) = 0;
    virtual BOOL ENABLE_TTL_OUT(BOOL EnableTtlOut) = 0;
    virtual BOOL TTL_IN(DWORD* TtlIn) = 0;
    virtual BOOL TTL_OUT(DWORD TtlOut) = 0;
    virtual BOOL ENABLE_FLASH_WRITE(BOOL IsUserFlashWriteEnabled) = 0;
    virtual BOOL READ_FLASH_ARRAY(void* UserFlash) = 0;
    virtual BOOL WRITE_FLASH_ARRAY(void* UserFlash) = 0;
    virtual BOOL GET_MODULE_DESCRIPTION(void* ModuleDescription) = 0;
    virtual BOOL SAVE_MODULE_DESCRIPTION(void* ModuleDescription) = 0;
};

class E2010Device : public DeviceInterface {
public:
    E2010Device();
    ~E2010Device() override;

    bool init() override;
    bool startStream() override;
    bool stopStream() override;
    int readSamples(int16_t* buffer, int max_samples, int timeout_ms) override;
    bool setParam(const char* param, double value) override;
    const char* getInfo() const override;

    // Дополнительные настройки
    bool setChannel(int channel);
    bool setSampleRate(double sample_rate_khz);
    bool setInputRangeMv(int range_mv); // 3000, 1000, 300

private:
    // Динамически загруженные функции
    typedef DWORD (*GetDllVersion_t)();
    typedef void* (*CreateLInstance_t)(const char*);

    HMODULE lusbapi_handle;
    ILE2010* pModule;
    std::string info_string;

    // Параметры
    int active_channel;
    double sample_rate_khz;
    int input_range_mv;

    // Буферы и состояние
    std::vector<int16_t> buffer_a, buffer_b;
    std::vector<OVERLAPPED> overlapped;
    int current_buffer;
    HANDLE read_thread;
    DWORD thread_id;
    volatile bool is_streaming;
    volatile bool stop_requested;

    // Вспомогательные методы
    bool loadLibrary();
    bool configureADC();
    void unloadLibrary();
    static DWORD WINAPI readThreadProc(LPVOID param);
};

#endif // ORBITA_E2010_DEVICE_H
