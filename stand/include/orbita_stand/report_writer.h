#pragma once

#include "orbita_stand/scenario.h"

#include <string>

namespace orbita::stand {

struct ReportPaths {
    // Compatibility alias: the primary operator-facing document is the
    // concise TU protocol.
    std::string html;
    std::string csv;
    std::string tuHtml;
    std::string productionHtml;
};

ReportPaths writeHtmlCsvReport(const ScenarioRunResult& run, const std::string& directory);

} // namespace orbita::stand
