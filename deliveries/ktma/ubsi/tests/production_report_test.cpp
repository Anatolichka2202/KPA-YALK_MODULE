#include "ktma/ubsi/production_report.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <chrono>
#include <iostream>
#include <QCoreApplication>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

ktma::ubsi::ProductionRunContext context()
{
    ktma::ubsi::ProductionRunContext value;
    value.productId = "product-1";
    value.productSerial = "UBSI-001";
    value.stage = ktma::registrar::Stage::Primary;
    value.package = ktma::ubsi::ProductionPackage::Yvp;
    value.scenarioCode = "PROD_YVP";
    value.composition = {
        {"yalk", "YALK-96", "YALK-001", true},
        {"ytp", "YTP", "YTP-001", false},
        {"yvp", "YVP", "YVP-001", true},
        {"power", "YP-P", "YPP-001", false}};
    return value;
}

orbita::stand::ScenarioRunResult run()
{
    orbita::stand::ScenarioRunResult value;
    value.runId = "run-001";
    value.scenarioId = "ktma.ubsi.production.yvp";
    value.scenarioTitle = "Production YVP";
    value.scenarioVersion = "1.0";
    value.profileVersion = "profile";
    value.objectSerial = "UBSI-001";
    value.startedAt = std::chrono::system_clock::now();
    value.finishedAt = value.startedAt;
    value.verdict = orbita::stand::RunVerdict::Error;

    orbita::stand::StepRunResult step;
    step.nodeId = "yvp";
    step.title = "YVP";
    step.verdict = orbita::stand::RunVerdict::Error;
    orbita::stand::MeasurementResult measurement;
    measurement.parameterKey = "ubsi.yvp.gain.1";
    measurement.title = "YVP 1";
    measurement.reference = 1.0;
    measurement.measured = 0.0;
    measurement.lowerLimit = 0.93;
    measurement.upperLimit = 1.07;
    measurement.unit = "mV/pC";
    measurement.verdict = orbita::stand::RunVerdict::Error;
    measurement.message = "generator unavailable";
    measurement.attributes["yalk_address"] = "88";
    measurement.attributes["reduced_error_percent"] = "-0.25";
    step.measurements.push_back(measurement);
    value.steps.push_back(step);
    value.events.push_back({value.startedAt, "supply", "SUPPLY", "current",
        orbita::stand::RunVerdict::NotRun,
        {{"setpoint_v", "27"}, {"volts", "26.98"}, {"amperes", "0.21"}}});
    return value;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        require(ktma::ubsi::productionStatusFromScenarioVerdict(
                    orbita::stand::RunVerdict::Ok)
                == ktma::ubsi::ProductionRunStatus::Norm,
            "OK must map to NORM");
        require(ktma::ubsi::productionStatusFromScenarioVerdict(
                    orbita::stand::RunVerdict::Fail)
                == ktma::ubsi::ProductionRunStatus::NotNorm,
            "FAIL must map to NOT_NORM");
        require(ktma::ubsi::productionStatusFromScenarioVerdict(
                    orbita::stand::RunVerdict::Error)
                == ktma::ubsi::ProductionRunStatus::StandError,
            "ERROR must map to STAND_ERROR");
        require(ktma::ubsi::productionStatusFromScenarioVerdict(
                    orbita::stand::RunVerdict::Incomplete)
                == ktma::ubsi::ProductionRunStatus::Incomplete,
            "INCOMPLETE must stay INCOMPLETE");
        require(ktma::ubsi::productionStatusFromScenarioVerdict(
                    orbita::stand::RunVerdict::Aborted)
                == ktma::ubsi::ProductionRunStatus::Stopped,
            "ABORTED must map to STOPPED");

        QTemporaryDir directory;
        require(directory.isValid(), "temporary report directory unavailable");
        const auto paths = ktma::ubsi::writeProductionReport(
            run(), context(), directory.path().toStdString());
        require(QFileInfo::exists(QString::fromStdString(paths.html)),
            "production HTML report missing");
        require(QFileInfo::exists(QString::fromStdString(paths.csv)),
            "production CSV report missing");

        const QDir output(directory.path());
        require(output.entryList({QStringLiteral("Протокол_ТУ_*")}, QDir::Files).isEmpty(),
            "Production reporter must never create a TU protocol");

        QFile html(QString::fromStdString(paths.html));
        require(html.open(QIODevice::ReadOnly), "cannot read production HTML");
        const QByteArray body = html.readAll();
        require(body.contains("STAND_ERROR") || body.contains("ОШИБКА СТЕНДА"),
            "production report lost stand-error semantics");
        require(body.contains("<svg") && body.contains("400 мА")
                    && body.contains("Отклонения измерений"),
            "production report must contain current and measurement charts");

        std::cout << "KTMA UBSI production report contract OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
