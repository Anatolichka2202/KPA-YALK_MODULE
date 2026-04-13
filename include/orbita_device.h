/**
 * @file orbita_device.h
 * @brief Типы и настройки устройств захвата (E20-10, LimeSDR, VISA).
 *
 * Определяет структуры для конфигурации устройств.
 * Функции установки устройств находятся в orbita.h.
 */

#ifndef ORBITA_DEVICE_H
#define ORBITA_DEVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Параметры для АЦП E20-10
typedef struct {
    int channel;               // номер канала (0..3)
    double sample_rate_khz;    // частота дискретизации в кГц (обычно 10000)
    int input_range_mv;        // 3000, 1000, 300 (мВ)
} orbita_e2010_config_t;

// Параметры для LimeSDR
typedef struct {
    double frequency_hz;       // частота гетеродина (Гц)
    double sample_rate_hz;     // частота дискретизации (Гц)
    int gain_db;               // усиление (дБ)
    int antenna;               // 0=LNA_H, 1=LNA_L, 2=LNA_W
    int enable_agc;            // 1 – автоматическая регулировка усиления
} orbita_limesdr_config_t;

// Параметры для вольтметра VISA
typedef struct {
    char resource[256];        // например "USB0::0x164E::0x0DAD::...::INSTR"
    int timeout_ms;            // таймаут операций
} orbita_visa_config_t;

#ifdef __cplusplus
}
#endif

#endif // ORBITA_DEVICE_H
