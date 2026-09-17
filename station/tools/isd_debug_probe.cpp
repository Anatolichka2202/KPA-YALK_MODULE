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
        << "  probe\n"
        << "  state\n"
        << "  trace\n"
        << "  clear_trace\n"
        << "  switch <type> <channel> <0|1> [owner]\n"
        << "  analog <channel> <value> <0|1> [owner]\n"
        << "  yalk_voltage <channel> <volts> [owner]\n"
        << "  yalk_off <channel> [owner]\n"
        << "  reset [owner]\n"
        << "  yalk_prepare [owner]\n"
        << "  release [owner]\n"
        << "  quit\n";
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
                    std::string type, channel, enabled, owner;
                    if (!(stream >> type >> channel >> enabled)) {
                        throw std::invalid_argument("switch requires: type channel 0|1 [owner]");
                    }
                    stream >> owner;
                    operation = "switch";
                    args = {{"type", type}, {"channel", channel},
                            {"enabled", truthy(enabled) ? "true" : "false"}};
                    if (!owner.empty()) args["owner"] = owner;
                } else if (command == "analog") {
                    std::string channel, value, enabled, owner;
                    if (!(stream >> channel >> value >> enabled)) {
                        throw std::invalid_argument("analog requires: channel value 0|1 [owner]");
                    }
                    stream >> owner;
                    operation = "analog";
                    args = {{"channel", channel}, {"value", value},
                            {"enabled", truthy(enabled) ? "true" : "false"}};
                    if (!owner.empty()) args["owner"] = owner;
                } else if (command == "yalk_voltage") {
                    std::string channel, volts, owner;
                    if (!(stream >> channel >> volts)) {
                        throw std::invalid_argument("yalk_voltage requires: channel volts [owner]");
                    }
                    stream >> owner;
                    operation = "yalk_set_voltage";
                    args = {{"channel", channel}, {"volts", volts}};
                    if (!owner.empty()) args["owner"] = owner;
                } else if (command == "yalk_off") {
                    std::string channel, owner;
                    if (!(stream >> channel)) {
                        throw std::invalid_argument("yalk_off requires: channel [owner]");
                    }
                    stream >> owner;
                    operation = "yalk_output_off";
                    args = {{"channel", channel}};
                    if (!owner.empty()) args["owner"] = owner;
                } else if (command == "reset" || command == "yalk_prepare") {
                    std::string owner;
                    stream >> owner;
                    operation = command;
                    if (!owner.empty()) args["owner"] = owner;
                } else if (command == "release") {
                    std::string owner = "legacy";
                    stream >> owner;
                    operation = "release_owner";
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
