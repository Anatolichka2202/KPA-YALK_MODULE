#pragma once

#include "backend/scenario_engine.h"

#include <memory>

namespace tu::hardware { class StandHardware; }

namespace tu::procedures {

// Процедуры отделены от исторически сложившегося совмещенного пути YALK/contact.
// Они содержат только те проверки, которые в настоящее время поддерживаются TU
// и подтвержденной 80-канальной аналоговой картой.
void registerVerifiedYalkProcedures(
    ScenarioEngine& engine,
    std::shared_ptr<hardware::StandHardware> hardware);

} // namespace tu::procedures
