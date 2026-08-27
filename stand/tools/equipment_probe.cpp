#include "orbita_stand/config.h"
#include "orbita_stand/equipment_runtime.h"

#include <QCoreApplication>

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 3) {
        std::cerr << "Usage: orbita_equipment_probe <stand-profile.yaml> <plugin-directory>\n";
        return EXIT_FAILURE;
    }

    try {
        const auto profile = orbita::stand::loadStandProfile(argv[1]);
        orbita::stand::EquipmentPluginManager manager;
        manager.loadDirectory(argv[2]);
        std::cout << "PROFILE " << profile.id << " " << profile.version << '\n';
        std::cout << "ACTIVE_OUTPUTS " << (profile.activeOutputsConfirmed ? "CONFIRMED" : "BLOCKED") << '\n';
        std::cout << "PLUGINS " << manager.plugins().size() << '\n';

        bool allReady = true;
        for (const auto& definition : profile.devices) {
            if (!definition.enabled) {
                const auto reason = definition.configuration.find("disabled_reason");
                std::cout << "DISABLED " << definition.id << " " << definition.pluginId;
                if (reason != definition.configuration.end()) std::cout << " " << reason->second;
                std::cout << '\n';
                continue;
            }
            try {
                auto config = definition.configuration;
                config["profile.active_outputs_confirmed"] =
                    profile.activeOutputsConfirmed ? "true" : "false";
                for (const auto& [key, value] : profile.routes) config["route." + key] = value;
                auto device = manager.createDevice(definition.pluginId, definition.id, config);
                if (definition.bindCapabilities.empty()) {
                    throw std::runtime_error("profile has no bound capability");
                }
                const auto response = device->invoke(definition.bindCapabilities.front(), "probe");
                const bool ready = response.find("status=no_data") == std::string::npos
                    && response.find("status=error") == std::string::npos
                    && response.find("alive=0") == std::string::npos
                    && response.find("alive=false") == std::string::npos;
                if (!ready) allReady = false;
                std::cout << (ready ? "OK " : "NOT_READY ") << definition.id << " "
                          << definition.pluginId << " " << response << '\n';
                device->safeStop();
            } catch (const std::exception& error) {
                allReady = false;
                std::cout << "NOT_READY " << definition.id << " " << definition.pluginId
                          << " " << error.what() << '\n';
            }
        }
        for (const auto& diagnostic : manager.diagnostics()) {
            std::cout << "PLUGIN_DIAGNOSTIC " << diagnostic << '\n';
        }
        return allReady ? EXIT_SUCCESS : 2;
    } catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
