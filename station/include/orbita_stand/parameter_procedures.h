#pragma once

namespace orbita::stand {
class ScenarioEngine;
class ParameterRepository;
// Действия читают общий снимок, который обновляет прикладной сборщик станции.
void registerParameterProcedures(ScenarioEngine& engine, ParameterRepository& parameters);
}
