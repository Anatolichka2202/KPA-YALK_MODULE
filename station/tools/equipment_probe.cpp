#include "orbita_stand/config.h"
#include "orbita_stand/equipment_runtime.h"

#include <QCoreApplication>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 3 && argc < 6) {
        std::cerr << "Usage:\n"
                  << "  orbita_equipment_probe <stand-profile.yaml> <plugin-directory>\n"
                  << "  orbita_equipment_probe <stand-profile.yaml> <plugin-directory> "
                     "<device-id> <capability> <operation> [key=value ...]\n";
        return EXIT_FAILURE;
    }

    try {
        const auto profile = orbita::stand::loadStandProfile(argv[1]);
        orbita::stand::EquipmentPluginManager manager;
        manager.loadDirectory(argv[2]);
        std::cout << "PROFILE " << profile.id << " " << profile.version << '\n';
        std::cout << "ACTIVE_OUTPUTS " << (profile.activeOutputsConfirmed ? "CONFIRMED" : "BLOCKED") << '\n';
        std::cout << "PLUGINS " << manager.plugins().size() << '\n';

        if (argc >= 6) {
            const std::string requestedDevice = argv[3];
            const auto definition = std::find_if(profile.devices.begin(), profile.devices.end(),
                [&](const auto& item) { return item.id == requestedDevice; });
            if (definition == profile.devices.end()) {
                throw std::runtime_error("Device is absent from profile: " + requestedDevice);
            }
            if (!definition->enabled) {
                throw std::runtime_error("Device is disabled in profile: " + requestedDevice);
            }
            auto config = definition->configuration;
            config["profile.active_outputs_confirmed"] =
                profile.activeOutputsConfirmed ? "true" : "false";
            for (const auto& [key, value] : profile.routes) config["route." + key] = value;
            auto device = manager.createDevice(definition->pluginId, definition->id, config);
            std::map<std::string, std::string> arguments;
            for (int index = 6; index < argc; ++index) {
                const std::string item = argv[index];
                const auto separator = item.find('=');
                if (separator == std::string::npos || separator == 0) {
                    throw std::invalid_argument("Expected key=value argument: " + item);
                }
                arguments[item.substr(0, separator)] = item.substr(separator + 1);
            }
            try {
                const auto response = device->invoke(argv[4], argv[5], arguments);
                std::cout << "INVOKE_OK " << requestedDevice << " " << argv[4] << "."
                          << argv[5] << '\n' << response << '\n';
                device->safeStop();
                return EXIT_SUCCESS;
            } catch (...) {
                device->safeStop();
                throw;
            }
        }

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
