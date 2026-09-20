#pragma once

#include "backend/scenario_engine.h"

#include <memory>

namespace tu::hardware { class StandHardware; }

namespace tu::procedures {

// Procedures kept separate from the historical combined YALK/contact path.
// They contain only checks that are currently supported by the TU and the
// confirmed 80-channel analog map.
void registerVerifiedYalkProcedures(
    ScenarioEngine& engine,
    std::shared_ptr<hardware::StandHardware> hardware);

} // namespace tu::procedures
