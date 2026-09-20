#pragma once

#include "backend/scenario_engine.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace tu::hardware { class StandHardware; }

namespace tu::procedures {

using OperatorResistanceInput = std::function<std::optional<double>(
    const std::string& title,
    const std::string& prompt,
    double targetOhms)>;

void registerYtpProcedures(ScenarioEngine& engine,
                           std::shared_ptr<hardware::StandHardware> hardware,
                           OperatorResistanceInput operatorInput);

} // namespace tu::procedures
