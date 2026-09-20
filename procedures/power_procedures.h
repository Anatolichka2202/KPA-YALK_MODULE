#pragma once

#include "backend/scenario_engine.h"

#include <memory>

namespace tu::hardware { class StandHardware; }

namespace tu::procedures {

void registerPowerProcedures(ScenarioEngine& engine,
                             std::shared_ptr<hardware::StandHardware> hardware);

// Временные честные заглушки только для ещё не перенесённых вертикальных
// срезов. Они не управляют оборудованием и всегда возвращают INCOMPLETE.
void registerUnavailableProcedures(ScenarioEngine& engine);

} // namespace tu::procedures
