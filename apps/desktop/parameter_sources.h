#pragma once

#include "orbita_stand/telemetry.h"
#include <memory>
#include <shared_mutex>
#include <string>

namespace orbita { class Orbita; }
namespace orbita::stand { class ISampleSource; }

// Адаптер не владеет физическим источником или декодером: оба принадлежат
// существующей сессии, поэтому монитор не открывает оборудование повторно.
std::unique_ptr<orbita::stand::IParameterSource> makeOrbitaParameters(
    orbita::Orbita& orbita, orbita::stand::ISampleSource& source, std::shared_mutex& channelsMutex);
std::unique_ptr<orbita::stand::IParameterSource> makeParameterSimulator(
    std::string id, std::string key, double value, std::string unit);
