#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tu::hardware {

struct AkipConfig {
    std::string portName;
    int baudRate = 115200;
    unsigned timeoutMilliseconds = 1200;
    std::string expectedIdentity = "AKIP-1160/6";
    double maximumVoltageV = 60.0;
    double overvoltageLimitV = 40.0;
    double maximumCurrentA = 10.0;
    double voltageSetpointToleranceV = 0.011;
    double currentSetpointToleranceA = 0.002;
    unsigned outputConfirmAttempts = 4;
    unsigned outputConfirmSettleMilliseconds = 150;
};

struct YalkUdpConfig {
    std::string remoteHost;
    std::string localHost;
    std::uint16_t port = 1113;
    unsigned receiveTimeoutMilliseconds = 800;
};

struct IsdConfig {
    std::string host;
    std::uint16_t port = 80;
    unsigned timeoutMilliseconds = 1500;
    unsigned serviceTimeoutMilliseconds = 10000;
};

struct V7Config {
    std::vector<std::string> resourceExpressions;
    unsigned timeoutMilliseconds = 5000;
    std::string dcVoltageCommand = "MEAS:VOLT:DC?";
    std::string acVoltageCommand = "MEAS:VOLT:AC?";
    std::string frequencyCommand = "MEAS:FREQ?";
};

struct RigolConfig {
    std::vector<std::string> resourceExpressions;
    unsigned timeoutMilliseconds = 2000;
};

struct StandConfig {
    AkipConfig supply;
    YalkUdpConfig yalk;
    IsdConfig isd;
    V7Config v7;
    RigolConfig generator;
};

StandConfig loadStandConfig(const std::filesystem::path& path);

} // namespace tu::hardware
