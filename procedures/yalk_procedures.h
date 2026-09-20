#pragma once

#include "backend/scenario_engine.h"

#include <memory>

namespace tu::hardware { class StandHardware; }

namespace tu::procedures {

void registerYalkProcedures(ScenarioEngine& engine,
                            std::shared_ptr<hardware::StandHardware> hardware);

} // namespace tu::procedures
