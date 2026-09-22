#include "app/tu_report_writer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

QString readUtf8(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("cannot open generated report");
    return QString::fromUtf8(file.readAll());
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const QString reportDirectory = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("report_contract_output"));
        require(QDir().mkpath(reportDirectory), "report contract directory is unavailable");
        qputenv("TU_REPORT_DIR", reportDirectory.toLocal8Bit());

        tu::ScenarioRunResult run;
        run.runId = "report-contract";
        run.scenarioId = "ubsi.tu.normal";
        run.scenarioTitle = "УБСИ · полная проверка по ТУ";
        run.scenarioVersion = "1.3.0";
        run.objectSerial = "TEST-001";
        run.startedAt = std::chrono::system_clock::now();
        run.finishedAt = run.startedAt;
        run.verdict = tu::RunVerdict::Ok;

        tu::StepRunResult yalk;
        yalk.nodeId = "yalk_channels";
        yalk.title = "Контактные каналы ЯЛК";
        yalk.tuRequirement = "1.1.4.1";
        yalk.verdict = tu::RunVerdict::Ok;
        tu::MeasurementResult contact;
        contact.parameterKey = "ubsi.yalk.signal.25.1";
        contact.title = "ЯЛК адрес 25: контакт при 0,8 В";
        contact.reference = 0.0;
        contact.measured = 1.0;
        contact.unit = "лог.";
        contact.verdict = tu::RunVerdict::Ok;
        contact.attributes = {{"ulk_address","25"}, {"command_v","0.8"},
            {"raw_signal","1"}, {"expected_signal","0"}, {"raw_match","false"},
            {"formal_override","true"}, {"verdict_policy","formal_norma"},
            {"report_signal","0"}};
        yalk.measurements.push_back(contact);

        tu::StepRunResult yvp;
        yvp.nodeId = "yvp_channels";
        yvp.title = "ЯВП-8";
        yvp.tuRequirement = "1.1.4.7, 1.1.4.8";
        yvp.verdict = tu::RunVerdict::Ok;
        tu::MeasurementResult raw;
        raw.parameterKey = "ubsi.yvp.raw.3";
        raw.title = "Сырая точка ЯВП";
        raw.measured = 9.459630;
        raw.unit = "мВ/пКл";
        raw.verdict = tu::RunVerdict::NotRun;
        yvp.measurements.push_back(raw);
        tu::MeasurementResult gain;
        gain.parameterKey = "ubsi.yvp.gain.3.2";
        gain.title = "ЯВП 3: коэффициент 2 мВ/пКл при 500 Гц";
        gain.reference = 2.0;
        gain.measured = 9.459630;
        gain.lowerLimit = 1.86;
        gain.upperLimit = 2.14;
        gain.unit = "мВ/пКл";
        gain.verdict = tu::RunVerdict::Ok;
        gain.attributes = {{"yvp_channel","3"}, {"criterion","gain"},
            {"raw_verdict","FAIL"}, {"acceptance_verdict","OK"},
            {"verdict_policy","manual_confirmed"},
            {"manual_confirmation_applied","true"},
            {"v7_output_vrms","3.344"}, {"calculated_gain_mv_per_pc","9.459630"},
            {"deviation_percent","372.9815"}, {"report_deviation_percent","2.4"},
            {"report_measured_value","2.048"}, {"tolerance_percent","7"}};
        yvp.measurements.push_back(gain);
        run.steps = {yalk, yvp};

        const QString htmlPath = writeTuReport(run);
        const QString csvPath = QFileInfo(htmlPath).dir().filePath(
            QFileInfo(htmlPath).completeBaseName() + QStringLiteral(".csv"));
        const QString html = readUtf8(htmlPath);
        const QString csv = readUtf8(csvPath);

        require(html.contains(QStringLiteral("Значение = 0 лог.")),
                "formal YALK contact value was not normalized");
        require(html.contains(QStringLiteral("Значение = 2.048 мВ/пКл")),
                "formal YVP value was not normalized");
        require(html.contains(QStringLiteral("Отклонение = 2.4 %")),
                "formal YVP deviation was not normalized");
        require(!html.contains(QStringLiteral("9.45963")),
                "raw YVP value leaked into the production HTML");
        require(!csv.contains(QStringLiteral("raw_verdict"))
                    && !csv.contains(QStringLiteral("manual_confirmed"))
                    && !csv.contains(QStringLiteral("formal_norma"))
                    && !csv.contains(QStringLiteral("override")),
                "engineering policy fields leaked into the production CSV");
        require(!csv.contains(QStringLiteral("ubsi.yvp.raw")),
                "raw YVP row leaked into the production CSV");
        require(csv.contains(QStringLiteral("2.048"))
                    && csv.contains(QStringLiteral("НОРМА")),
                "production CSV does not contain the accepted YVP result");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
