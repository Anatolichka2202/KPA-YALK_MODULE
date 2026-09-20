#include "hardware/stand_config.h"

#include <yaml-cpp/yaml.h>

#include <stdexcept>

namespace tu::hardware {
namespace {

template <typename T>
T required(const YAML::Node& node, const char* key)
{
    const auto value = node[key];
    if (!value) throw std::runtime_error(std::string("Не задан параметр стенда: ") + key);
    return value.as<T>();
}

template <typename T>
T optional(const YAML::Node& node, const char* key, T fallback)
{
    const auto value = node[key];
    return value ? value.as<T>() : fallback;
}

std::vector<std::string> resources(const YAML::Node& node)
{
    std::vector<std::string> result;
    if (const auto value = node["resource"]; value) result.push_back(value.as<std::string>());
    if (const auto value = node["fallback_resource"]; value) result.push_back(value.as<std::string>());
    if (const auto values = node["resources"]; values && values.IsSequence()) {
        for (const auto& value : values) result.push_back(value.as<std::string>());
    }
    if (result.empty()) throw std::runtime_error("Для VISA-прибора не задан resource");
    return result;
}

} // namespace

StandConfig loadStandConfig(const std::filesystem::path& path)
{
    const YAML::Node root = YAML::LoadFile(path.string());
    const auto supply = root["power_supply"];
    const auto yalk = root["yalk_adapter"];
    const auto isd = root["isd"];
    const auto v7 = root["v7"];
    const auto generator = root["generator"];
    if (!supply || !yalk || !isd || !v7 || !generator) {
        throw std::runtime_error(
            "Профиль стенда должен содержать power_supply, yalk_adapter, isd, v7 и generator");
    }

    StandConfig config;
    config.supply.portName = required<std::string>(supply, "port");
    config.supply.baudRate = optional<int>(supply, "baud", 115200);
    config.supply.timeoutMilliseconds = optional<unsigned>(supply, "timeout_ms", 1200);
    config.supply.expectedIdentity = optional<std::string>(supply, "expected_idn", "AKIP-1160/6");
    config.supply.maximumVoltageV = optional<double>(supply, "max_voltage_v", 60.0);
    config.supply.overvoltageLimitV = optional<double>(supply, "overvoltage_limit_v", 40.0);
    config.supply.maximumCurrentA = optional<double>(supply, "max_current_a", 10.0);
    config.supply.voltageSetpointToleranceV = optional<double>(
        supply, "setpoint_voltage_tolerance_v", 0.011);
    config.supply.currentSetpointToleranceA = optional<double>(
        supply, "setpoint_current_tolerance_a", 0.002);
    config.supply.outputConfirmAttempts = optional<unsigned>(supply, "output_confirm_attempts", 4);
    config.supply.outputConfirmSettleMilliseconds = optional<unsigned>(
        supply, "output_confirm_settle_ms", 150);

    config.yalk.remoteHost = required<std::string>(yalk, "host");
    config.yalk.localHost = required<std::string>(yalk, "local_address");
    config.yalk.port = static_cast<std::uint16_t>(optional<unsigned>(yalk, "data_port", 1113));
    config.yalk.receiveTimeoutMilliseconds = optional<unsigned>(yalk, "timeout_ms", 800);

    config.isd.host = required<std::string>(isd, "host");
    config.isd.port = static_cast<std::uint16_t>(optional<unsigned>(isd, "port", 80));
    config.isd.timeoutMilliseconds = optional<unsigned>(isd, "timeout_ms", 1500);

    config.v7.resourceExpressions = resources(v7);
    config.v7.timeoutMilliseconds = optional<unsigned>(v7, "timeout_ms", 5000);
    config.v7.dcVoltageCommand = optional<std::string>(v7, "voltage_command", "MEAS:VOLT:DC?");
    config.v7.acVoltageCommand = optional<std::string>(v7, "ac_voltage_command", "MEAS:VOLT:AC?");
    config.v7.frequencyCommand = optional<std::string>(v7, "frequency_command", "MEAS:FREQ?");

    config.generator.resourceExpressions = resources(generator);
    config.generator.timeoutMilliseconds = optional<unsigned>(generator, "timeout_ms", 2000);

    if (config.supply.portName.empty()) throw std::runtime_error("Пустой COM-порт АКИП");
    if (config.supply.baudRate <= 0 || config.supply.timeoutMilliseconds == 0)
        throw std::runtime_error("Некорректные параметры последовательного порта АКИП");
    if (config.supply.overvoltageLimitV <= 0.0
        || config.supply.overvoltageLimitV > config.supply.maximumVoltageV)
        throw std::runtime_error("Некорректный предел OVP АКИП в профиле стенда");
    if (config.yalk.remoteHost.empty() || config.yalk.localHost.empty() || !config.yalk.port)
        throw std::runtime_error("Некорректная UDP-конфигурация адаптера УБСИ");
    if (config.isd.host.empty() || !config.isd.port || !config.isd.timeoutMilliseconds)
        throw std::runtime_error("Некорректная конфигурация ИСД");

    return config;
}

} // namespace tu::hardware
