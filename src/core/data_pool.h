/**
 * @file data_pool.hpp
 * @brief Хранилище текущих значений каналов (аналоговые, контактные, быстрые, температурные, БУС).
 *
 * Потокобезопасен (использует std::mutex). Предоставляет методы для обновления и чтения значений.
 */

#pragma once

#include <vector>
#include <mutex>

namespace orbita {

class DataPool {
public:
    DataPool() = default;
    ~DataPool() = default;

    // Запрет копирования
    DataPool(const DataPool&) = delete;
    DataPool& operator=(const DataPool&) = delete;

    // Разрешение перемещения
    DataPool(DataPool&&) = delete;
    DataPool& operator=(DataPool&&) = delete;
    /**
     * @brief Изменяет размеры массивов под заданное количество каналов каждого типа.
     * @param analog_count   количество аналоговых каналов (T01, T01-01)
     * @param contact_count  количество контактных каналов (T05)
     * @param fast_count     количество быстрых каналов (T21, T22, T24)
     * @param temp_count     количество температурных каналов (T11)
     * @param bus_count      количество каналов БУС (T25)
     * @throws std::bad_alloc при ошибке выделения памяти
     */
    void resize(int analog_count, int contact_count, int fast_count, int temp_count, int bus_count);

    // -----------------------------------------------------------------
    // Установка значений (вызывается из декодера при поступлении новой группы)
    // -----------------------------------------------------------------
    void setAnalog(int index, double value);
    void setContact(int index, int value);
    void setFast(int index, int value);
    void setTemperature(int index, int value);
    void setBus(int index, int value);

    // -----------------------------------------------------------------
    // Чтение значений (вызывается из пользовательского API)
    // -----------------------------------------------------------------
    double getAnalog(int index) const;
    int    getContact(int index) const;
    int    getFast(int index) const;
    int    getTemperature(int index) const;
    int    getBus(int index) const;

    /**
     * @brief Проверяет, были ли обновлены какие-либо данные с момента последнего вызова clearNewFlag().
     * @return true если есть новые данные, иначе false
     */
    bool hasNewData() const;

    /**
     * @brief Сбрасывает флаг новых данных.
     */
    void clearNewFlag();

private:
    mutable std::mutex mutex_;
    std::vector<double> analog_;
    std::vector<int> contact_;
    std::vector<int> fast_;
    std::vector<int> temperature_;
    std::vector<int> bus_;
    bool new_data_flag_ = false;
};

} // namespace orbita
