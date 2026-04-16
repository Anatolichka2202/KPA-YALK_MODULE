#include "data_pool.h"
#include <algorithm>
#include <stdexcept>

namespace orbita {

void DataPool::resize(int analog_count, int contact_count, int fast_count, int temp_count, int bus_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    analog_.resize(analog_count);
    contact_.resize(contact_count);
    fast_.resize(fast_count);
    temperature_.resize(temp_count);
    bus_.resize(bus_count);
    // new_data_flag_ не сбрасываем, т.к. изменение размеров не считается новыми данными
}

void DataPool::setAnalog(int index, double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || static_cast<size_t>(index) >= analog_.size()) {
        throw std::out_of_range("DataPool::setAnalog: index out of range");
    }
    analog_[index] = value;
    new_data_flag_ = true;
}

void DataPool::setContact(int index, int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || static_cast<size_t>(index) >= contact_.size()) {
        throw std::out_of_range("DataPool::setContact: index out of range");
    }
    contact_[index] = value;
    new_data_flag_ = true;
}

void DataPool::setFast(int index, int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || static_cast<size_t>(index) >= fast_.size()) {
        throw std::out_of_range("DataPool::setFast: index out of range");
    }
    fast_[index] = value;
    new_data_flag_ = true;
}

void DataPool::setTemperature(int index, int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || static_cast<size_t>(index) >= temperature_.size()) {
        throw std::out_of_range("DataPool::setTemperature: index out of range");
    }
    temperature_[index] = value;
    new_data_flag_ = true;
}

void DataPool::setBus(int index, int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || static_cast<size_t>(index) >= bus_.size()) {
        throw std::out_of_range("DataPool::setBus: index out of range");
    }
    bus_[index] = value;
    new_data_flag_ = true;
}

double DataPool::getAnalog(int index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || static_cast<size_t>(index) >= analog_.size()) {
        return 0.0; // или бросить исключение? API возвращает 0 по умолчанию
    }
    return analog_[index];
}

int DataPool::getContact(int index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || static_cast<size_t>(index) >= contact_.size()) {
        return 0;
    }
    return contact_[index];
}

int DataPool::getFast(int index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || static_cast<size_t>(index) >= fast_.size()) {
        return 0;
    }
    return fast_[index];
}

int DataPool::getTemperature(int index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || static_cast<size_t>(index) >= temperature_.size()) {
        return 0;
    }
    return temperature_[index];
}

int DataPool::getBus(int index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || static_cast<size_t>(index) >= bus_.size()) {
        return 0;
    }
    return bus_[index];
}

bool DataPool::hasNewData() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return new_data_flag_;
}

void DataPool::clearNewFlag() {
    std::lock_guard<std::mutex> lock(mutex_);
    new_data_flag_ = false;
}

} // namespace orbita
