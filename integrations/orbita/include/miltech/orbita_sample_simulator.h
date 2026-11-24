#pragma once

#include <memory>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace orbita::stand { class ISampleSource; }

namespace miltech::integration {

// Генератор полезных 11 бит по порядковому номеру слова. Маркеры и сырые
// отсчёты по-прежнему формирует существующий имитатор.
std::unique_ptr<orbita::stand::ISampleSource> createOrbitaSampleSimulator(
    std::function<uint16_t(std::size_t)> word);
std::unique_ptr<orbita::stand::ISampleSource> createOrbitaM8SampleSimulator(
    std::function<uint16_t(std::size_t)> word);

} // namespace miltech::integration
