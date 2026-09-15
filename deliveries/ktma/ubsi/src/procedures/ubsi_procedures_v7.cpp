#include "ktma/ubsi/procedures.h"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace orbita::stand {

// Previous layer. CMake renames the public registration symbol of
// ubsi_procedures_rokt.cpp to this name, then this translation unit installs
// the production V7+ISD alias last.
void registerRoktUbsiProcedures(ScenarioEngine& engine);

namespace {

std::string argument(const ScenarioNode& node, const std::string& key,
                     std::string fallback = {})
{
    const auto found = node.arguments.find(key);
    return found == node.arguments.end() ? std::move(fallback) : found->second;
}

bool flag(const ScenarioNode& node, const std::string& key, bool fallback = false)
{
    const auto text = argument(node, key);
    if (text.empty()) return fallback;
    if (text == "true" || text == "1" || text == "yes" || text == "on") return true;
    if (text == "false" || text == "0" || text == "no" || text == "off") return false;
    throw std::invalid_argument("Некорректный логический аргумент " + key);
}

unsigned natural(const ScenarioNode& node, const std::string& key, unsigned fallback = 0)
{
    const auto text = argument(node, key);
    if (text.empty()) return fallback;
    std::size_t parsed = 0;
    const auto value = std::stoul(text, &parsed, 0);
    if (parsed != text.size()) throw std::invalid_argument("Некорректный аргумент " + key);
    return static_cast<unsigned>(value);
}

std::vector<double> numbers(const ScenarioNode& node, const std::string& key)
{
    std::vector<double> result;
    std::stringstream stream(argument(node, key));
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (item.empty()) continue;
        std::size_t parsed = 0;
        const double value = std::stod(item, &parsed);
        if (parsed != item.size() || !std::isfinite(value))
            throw std::invalid_argument("Некорректный аргумент " + key);
        result.push_back(value);
    }
    return result;
}

bool hasArgument(const ScenarioNode& node, const std::string& key)
{
    const auto found = node.arguments.find(key);
    return found != node.arguments.end() && !found->second.empty();
}

std::string gainMappingKey(double gain)
{
    if (std::abs(gain - 0.25) < 1e-9) return "gain_0_25_contacts";
    if (std::abs(gain - 0.5) < 1e-9) return "gain_0_5_contacts";
    if (std::abs(gain - 1.0) < 1e-9) return "gain_1_contacts";
    if (std::abs(gain - 2.0) < 1e-9) return "gain_2_contacts";
    if (std::abs(gain - 4.0) < 1e-9) return "gain_4_contacts";
    if (std::abs(gain - 8.0) < 1e-9) return "gain_8_contacts";
    if (std::abs(gain - 32.0) < 1e-9) return "gain_32_contacts";
    throw std::invalid_argument("ЯВП: неподдержанный коэффициент усиления");
}

ProcedureResult yvpV7Isd(const ScenarioNode& node, ProcedureContext&)
{
    const unsigned channels = natural(node, "channel_count", 8);
    const auto gains = numbers(node, "gains_mv_per_pcl");
    const auto frequencies = numbers(node, "frequencies_hz");

    if (channels != 8)
        throw std::invalid_argument("ЯВП-8: production backend рассчитан на 8 каналов");
    if (gains.empty() || frequencies.empty())
        throw std::invalid_argument("ЯВП-8: не заданы коэффициенты усиления или частоты");

    // Критический fail-safe: до подтверждения трассировки Э3 процедура не
    // должна обращаться ни к ИСД, ни к Rigol, ни к В7. Именно этот backend
    // владеет production alias `ubsi.yvp`; ROKT остаётся отдельным diagnostic
    // path (`yvp.rokt`, `yvp.enter_mode`).
    if (!flag(node, "mapping_confirmed", false)) {
        return {RunVerdict::Incomplete,
            "ЯВП-8: карта коммутации ИСД по Э3 не подтверждена; ИСД и Rigol не активировались",
            {}};
    }
    if (!flag(node, "active_outputs_confirmed", false)) {
        return {RunVerdict::Incomplete,
            "ЯВП-8: активные воздействия не разрешены сценарием; ИСД и Rigol не активировались",
            {}};
    }

    // Не подменяем отсутствующую Э3 догадками. После commissioning профиль
    // обязан содержать явные типы коммутации и контакты каждого входа/выхода
    // и каждого коэффициента. Пока хотя бы одного поля нет, hardware остаётся
    // нетронутым даже при ошибочно выставленном mapping_confirmed=true.
    for (const char* key : {"input_switch_type", "gain_switch_type",
                            "measurement_switch_type"}) {
        if (!hasArgument(node, key)) {
            return {RunVerdict::Incomplete,
                "ЯВП-8: mapping_confirmed=true, но не задан полный исполняемый контракт Э3 ИСД; воздействие заблокировано",
                {}};
        }
    }
    for (unsigned channel = 1; channel <= channels; ++channel) {
        if (!hasArgument(node, "input_" + std::to_string(channel) + "_contacts")
            || !hasArgument(node, "measurement_" + std::to_string(channel) + "_contacts")) {
            return {RunVerdict::Incomplete,
                "ЯВП-8: mapping_confirmed=true, но контакты входа/измерения заданы не для всех 8 каналов; воздействие заблокировано",
                {}};
        }
    }
    for (const double gain : gains) {
        if (!hasArgument(node, gainMappingKey(gain))) {
            return {RunVerdict::Incomplete,
                "ЯВП-8: mapping_confirmed=true, но карта выбора коэффициентов неполна; воздействие заблокировано",
                {}};
        }
    }

    // Исполняемую последовательность V7+ISD нельзя достраивать без
    // подтверждённого порядка коммутации входа/выхода и точки, в которой В7
    // измеряет опорное воздействие. Это отдельный commissioning milestone.
    return {RunVerdict::Incomplete,
        "ЯВП-8: карта Э3 описана, но измерительная последовательность V7+ISD ещё не подтверждена на стенде; воздействие не выполнялось",
        {}};
}

} // namespace

void registerUbsiProcedures(ScenarioEngine& engine)
{
    registerRoktUbsiProcedures(engine);
    engine.registerProcedure("yvp.v7", yvpV7Isd);
    engine.registerProcedure("ubsi.yvp", yvpV7Isd);
}

} // namespace orbita::stand
