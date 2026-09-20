#pragma once

#include "backend/scenario_engine.h"

#include <functional>
#include <memory>
#include <string>

namespace tu::hardware { class StandHardware; }

namespace tu::procedures {

using OperatorConfirm = std::function<bool(const std::string& title,
                                           const std::string& prompt)>;

void registerYtpProcedures(ScenarioEngine& engine,
                           std::shared_ptr<hardware::StandHardware> hardware,
                           OperatorConfirm operatorConfirm);

} // namespace tu::procedures
