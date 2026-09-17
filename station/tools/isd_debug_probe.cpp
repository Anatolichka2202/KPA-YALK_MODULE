#include "orbita_stand/config.h"
#include "orbita_stand/equipment_runtime.h"

#include <QCoreApplication>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

bool truthy(const std::string& value)
{
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

void printHelp()
{
    std::cout
        << "Commands:\n"
        << "  help\n"
        << "  probe                         HTTP connectivity only\n"
        << "  state                         session state + owned routes\n"
        << "  trace\n"
        << "  clear_trace\n"
        << "  switch <type> <channel> <0|1> [owner]\n"
        << "  analog <channel> <value> <0|1> [owner]\n"
        << "  yalk_voltage <channel> <volts> [owner]\n"
        << "  yalk_off <channel> [owner]\n"
        << "  release [owner]\n"
        << "  reset [owner]                 DANGEROUS SERVICE: firmware type=4, one long all-channels-off sweep\n"
        << "                                  never used by automatic cleanup/recovery\n"
        << "  quit\n";
}

std::string readOwner(std::stringstream& stream)
{
    std::string owner;
    stream >> owner;
    return owner.empty() ? "debug" : owner;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 3 && argc != 4) {
        std::cerr << "Usage: orbita_isd_debug_probe <stand-profile.yaml> <plugin-directory> [device-id]\n";
        return EXIT_FAILURE;
    }

    try {
        const auto profile = orbita::stand::loadStandProfile(argv[1]);
        const std::string requestedId = argc == 4 ? argv[3] : "isd";
        const auto definition = std::find_if(profile.devices.begin(), profile.devices.end(),
            [&](const auto& item) { return item.id == requestedId; });
        if (definition == profile.devices.end()) {
            throw std::runtime_error("ISD device is absent from profile: " + requestedId);
        }
        if (!definition->enabled) {
            throw std::runtime_error("ISD device is disabled in profile: " + requestedId);
        }

        orbita::stand::EquipmentPluginManager manager;
        manager.loadDirectory(argv[2]);
        auto config = definition->configuration;
        config["profile.active_outputs_confirmed"] =
            profile.activeOutputsConfirmed ? "true" : "false";
        for (const auto& [key, value] : profile.routes) config["route." + key] = value;
        auto device = manager.createDevice(definition->pluginId, definition->id, config);

        std::cout << "ISD DEBUG READY device=" << requestedId
                  << " plugin=" << definition->pluginId
                  << " active_outputs=" << (profile.activeOutputsConfirmed ? "confirmed" : "blocked")
                  << "\n";
        printHelp();

        std::string line;
        while (std::cout << "isd> " && std::getline(std::cin, line)) {
            std::stringstream stream(line);
            std::string command;
            stream >> command;
            if (command.empty()) continue;
            if (command == "quit" || command == "exit") break;
            if (command == "help") {
                printHelp();
                continue;
            }

            try {
                std::string operation;
                std::map<std::string, std::string> args;
                if (command == "probe" || command == "state" || command == "trace"
                    || command == "clear_trace") {
                    operation = command;
                } else if (command == "switch") {
                    std::string type, channel, enabled;
                    if (!(stream >> type >> channel >> enabled)) {
                        throw std::invalid_argument("switch requires: type channel 0|1 [owner]");
                    }
                    operation = "switch";
                    args = {{"type", type}, {"channel", channel},
                            {"enabled", truthy(enabled) ? "true" : "false"},
                            {"owner", readOwner(stream)}};
                } else if (command == "analog") {
                    std::string channel, value, enabled;
                    if (!(stream >> channel >> value >> enabled)) {
                        throw std::invalid_argument("analog requires: channel value 0|1 [owner]");
                    }
                    operation = "analog";
                    args = {{"channel", channel}, {"value", value},
                            {"enabled", truthy(enabled) ? "true" : "false"},
                            {"owner", readOwner(stream)}};
                } else if (command == "yalk_voltage") {
                    std::string channel, volts;
                    if (!(stream >> channel >> volts)) {
                        throw std::invalid_argument("yalk_voltage requires: channel volts [owner]");
                    }
                    operation = "yalk_set_voltage";
                    args = {{"channel", channel}, {"volts", volts}, {"owner", readOwner(stream)}};
                } else if (command == "yalk_off") {
                    std::string channel;
                    if (!(stream >> channel)) {
                        throw std::invalid_argument("yalk_off requires: channel [owner]");
                    }
                    operation = "yalk_output_off";
                    args = {{"channel", channel}, {"owner", readOwner(stream)}};
                } else if (command == "release") {
                    operation = "release_owner";
                    args["owner"] = readOwner(stream);
                } else if (command == "reset") {
                    const auto owner = readOwner(stream);
                    std::cout
                        << "WARNING: SERVICE FULL RESET sends firmware /type=4num=1 once.\n"
                        << "It may run a long time and may partially execute if an internal UART module fails.\n";
                    operation = "service_full_reset";
                    args["owner"] = owner;
                } else {
                    throw std::invalid_argument("Unknown command: " + command);
                }

                const auto response = device->invoke(
                    "stand.switch_matrix", operation, args);
                std::cout << response;
                if (!response.empty() && response.back() != '\n') std::cout << '\n';
            } catch (const std::exception& error) {
                std::cout << "ERROR " << error.what() << '\n';
            }
        }

        device->safeStop();
        std::cout << "ISD DEBUG STOPPED\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
