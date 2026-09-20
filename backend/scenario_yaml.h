#pragma once

#include "backend/scenario_engine.h"

#include <filesystem>

namespace tu {

ScenarioDefinition loadScenarioYaml(const std::filesystem::path& path);

} // namespace tu
