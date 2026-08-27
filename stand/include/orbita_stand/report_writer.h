#pragma once

#include "orbita_stand/scenario.h"

#include <string>

namespace orbita::stand {

struct ReportPaths {
    std::string html;
    std::string csv;
};

ReportPaths writeHtmlCsvReport(const ScenarioRunResult& run, const std::string& directory);

} // namespace orbita::stand
