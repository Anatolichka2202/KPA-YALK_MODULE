#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

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

struct StandConfig {
    AkipConfig supply;
    YalkUdpConfig yalk;
};

StandConfig loadStandConfig(const std::filesystem::path& path);

} // namespace tu::hardware
