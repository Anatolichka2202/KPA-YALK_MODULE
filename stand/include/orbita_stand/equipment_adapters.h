#pragma once

#include "orbita_stand/yalk_analog_procedure.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace orbita::stand {

struct IsdHttpConfig {
    std::string host;
    std::uint16_t port = 80;
    unsigned timeoutMilliseconds = 1500;
    unsigned switchType = 2;
    std::vector<unsigned> resetChannels;
};

class IsdHttpRouter final : public IIsdRouter {
public:
    explicit IsdHttpRouter(IsdHttpConfig config);
    ~IsdHttpRouter() override;
    std::string probe();
    void reset() override;
    void connectChannel(unsigned channel) override;
    void disconnectChannel(unsigned channel) override;
    void setSwitch(unsigned type, unsigned channel, bool enabled);
    void setAnalog(unsigned channel, unsigned value, bool enabled);
    static std::string switchPath(unsigned type, unsigned channel, bool enabled);
    static std::string analogPath(unsigned channel, unsigned value, bool enabled);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct RigolVisaConfig {
    std::vector<std::string> resourceExpressions{
        "USB[0-9]*::0x1AB1::0x0588::?*INSTR",
        "USB[0-9]*::0x09C4::0x0400::?*INSTR"
    };
    unsigned timeoutMilliseconds = 2000;
};

class RigolVisaGenerator final {
public:
    explicit RigolVisaGenerator(RigolVisaConfig config = {});
    ~RigolVisaGenerator();
    std::string identity();
    void setSine(unsigned channel, double frequencyHz, double amplitudeVpp,
                 double offsetVolts = 0.0);
    void output(unsigned channel, bool enabled);
    const std::string& resourceName() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct R4831SerialConfig {
    std::string portName;
    int baudRate = 9600;
    unsigned timeoutMilliseconds = 1000;
    bool decimalComma = false;
};

class R4831SerialAdapter final {
public:
    explicit R4831SerialAdapter(R4831SerialConfig config);
    ~R4831SerialAdapter();
    std::string probe();
    void setResistance(double ohms);
    static std::string resistanceCommand(double ohms, bool decimalComma = false);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct LegacyUdpPowerSupplyConfig {
    std::string host;
    std::uint16_t commandPort = 4001;
    std::uint16_t localReplyPort = 6008;
    unsigned timeoutMilliseconds = 1500;
    bool allowLegacyCommands = false;
};

class LegacyUdpPowerSupply final : public IVoltageSource {
public:
    explicit LegacyUdpPowerSupply(LegacyUdpPowerSupplyConfig config);
    ~LegacyUdpPowerSupply() override;
    std::string probe();
    void setVoltage(double volts) override;
    void outputOn() override;
    void outputOff() override;
    void setCurrent(double amperes);
    static std::string voltageCommand(double volts);
    static std::string currentCommand(double amperes);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct UbsiUdpConfig {
    std::string adapterHost;
    std::string localAddress;
    std::uint16_t commandAndDataPort = 1001;
    std::uint16_t acknowledgementPort = 1101;
    unsigned timeoutMilliseconds = 1500;
};

class UbsiUdpAdapter final {
public:
    explicit UbsiUdpAdapter(UbsiUdpConfig config);
    ~UbsiUdpAdapter();
    bool waitForPassivePacket();
    void selectMode(std::uint8_t mode, bool single = false);
    std::vector<std::uint8_t> receiveRawPacket();
    static std::vector<std::uint8_t> modeCommand(std::uint8_t mode, bool single = false);
    static std::vector<std::uint16_t> decodeYalkPacket(
        const std::vector<std::uint8_t>& packet, std::uint16_t mask);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orbita::stand
