#pragma once

#include "orbita_stand/equipment_runtime.h"
#include "orbita_stand/scenario.h"

#include <map>
#include <string>
#include <vector>

namespace orbita::stand {

struct DeviceProfile {
    std::string id;
    std::string pluginId;
    bool enabled = true;
    std::vector<std::string> bindCapabilities;
    std::map<std::string, std::string> configuration;
};

struct StandProfile {
    std::string id;
    std::string version;
    std::string title;
    bool activeOutputsConfirmed = false;
    std::vector<DeviceProfile> devices;
    std::map<std::string, std::string> routes;
};

StandProfile loadStandProfile(const std::string& path);
ScenarioDefinition loadScenarioYaml(const std::string& path);
void instantiateProfile(
    const StandProfile& profile,
    EquipmentPluginManager& manager,
    EquipmentRegistry& registry,
    std::vector<std::shared_ptr<EquipmentDevice>>& devices);

} // namespace orbita::stand
