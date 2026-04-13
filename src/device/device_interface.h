/**
 * @file device_interface.h
 * @brief Абстрактный интерфейс для всех устройств захвата данных.
 *
 * Определяет общие методы: инициализация, запуск/остановка потока, чтение данных.
 */

#ifndef ORBITA_DEVICE_INTERFACE_H
#define ORBITA_DEVICE_INTERFACE_H

#include <cstdint>
#include <vector>

class DeviceInterface {
public:
    virtual ~DeviceInterface() = default;

    // Инициализация устройства (выделение ресурсов, открытие порта и т.д.)
    virtual bool init() = 0;

    // Запуск непрерывного потока данных
    virtual bool startStream() = 0;

    // Остановка потока
    virtual bool stopStream() = 0;

    // Получение очередного блока данных (синхронно, с таймаутом)
    // Возвращает количество полученных отсчётов (16-битных), 0 при таймауте, -1 при ошибке
    virtual int readSamples(int16_t* buffer, int max_samples, int timeout_ms) = 0;

    // Настройка параметров (частоты дискретизации, усиления и т.д.)
    virtual bool setParam(const char* param, double value) = 0;

    // Получение информации об устройстве (строковое описание)
    virtual const char* getInfo() const = 0;
};

#endif // ORBITA_DEVICE_INTERFACE_H
