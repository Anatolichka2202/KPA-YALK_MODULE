#include "backend/scenario_engine.h"
#include "backend/scenario_yaml.h"
#include "hardware/stand_config.h"
#include "hardware/stand_hardware.h"
#include "procedures/power_procedures.h"
#include "procedures/yalk_procedures.h"

#include <QCoreApplication>
#include <QDateTime>

#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void writeLine(std::ofstream& output, const std::string& line)
{
    const auto stamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString();
    output << stamp << ' ' << line << '\n';
    output.flush();
    std::cout << stamp << ' ' << line << std::endl;
}

std::string compact(std::string value)
{
    for (char& symbol : value) {
        if (symbol == '\r' || symbol == '\n' || symbol == '\t') symbol = ' ';
    }
    return value;
}

std::string formatEvent(const tu::RunEvent& event)
{
    std::string line = "EVENT node=" + event.nodeId + " stage=" + event.stage
        + " verdict=" + tu::toString(event.verdict) + " message=" + compact(event.message);
    for (const auto& [key, value] : event.data)
        line += " " + key + "=" + compact(value);
    return line;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 5) {
        std::cerr << "Usage: tu_yalk_trace <stand.yaml> <trace.yaml> <serial> <trace.txt>\n";
        return 2;
    }

    std::ofstream output(argv[4], std::ios::out | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot open trace output file");

    auto hardware = std::make_shared<tu::hardware::StandHardware>(
        tu::hardware::loadStandConfig(argv[1]));
    hardware->isd().setTraceSink([&output](const std::string& path, int status,
                                            const std::string& response) {
        writeLine(output, "ISD path=" + path + " http_status=" + std::to_string(status)
            + " response=" + compact(response));
    });

    tu::ScenarioEngine engine;
    tu::procedures::registerPowerProcedures(engine, hardware);
    tu::procedures::registerYalkProcedures(engine, hardware);

    try {
        const auto scenario = tu::loadScenarioYaml(argv[2]);
        const auto result = engine.run(scenario, argv[3], [&output](const tu::RunEvent& event) {
            writeLine(output, formatEvent(event));
        });
        hardware->safeStop();
        writeLine(output, "RUN verdict=" + std::string(tu::toString(result.verdict))
            + " steps=" + std::to_string(result.steps.size()));
        return result.verdict == tu::RunVerdict::Ok ? 0 : 1;
    } catch (const std::exception& error) {
        hardware->safeStop();
        writeLine(output, std::string("FATAL ") + error.what());
        return 3;
    }
}
