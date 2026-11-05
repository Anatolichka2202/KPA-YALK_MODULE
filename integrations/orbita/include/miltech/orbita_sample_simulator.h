#pragma once

#include <memory>

namespace orbita::stand { class ISampleSource; }

namespace miltech::integration {

// Создаёт локальный имитатор сырого сигнала M16. Значения синтетические и
// предназначены только для проверки тракта станции и интерфейса.
std::unique_ptr<orbita::stand::ISampleSource> createOrbitaSampleSimulator();

} // namespace miltech::integration
