#pragma once

#include "ktma/ubsi/production.h"
#include "orbita_stand/scenario.h"

#include <string>

namespace ktma::ubsi {

struct ProductionReportPaths
{
    std::string html;
    std::string csv;
};

ProductionRunStatus productionStatusFromScenarioVerdict(
    orbita::stand::RunVerdict verdict) noexcept;

ProductionReportPaths writeProductionReport(
    const orbita::stand::ScenarioRunResult& run,
    const ProductionRunContext& context,
    const std::string& directoryPath);

} // namespace ktma::ubsi
