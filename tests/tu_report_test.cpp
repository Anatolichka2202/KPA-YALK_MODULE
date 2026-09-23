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
            {"v7_v","0.805"},
            {"raw_signal","1"}, {"expected_signal","0"}, {"raw_match","false"},
            {"formal_override","true"}, {"verdict_policy","formal_norma"},
            {"report_signal","0"}};
        yalk.measurements.push_back(contact);
        tu::MeasurementResult analog;
        analog.parameterKey = "ubsi.yalk.channel.25.1";
        analog.reference = 3.1;
        analog.measured = 3.098;
        analog.unit = "В";
        analog.verdict = tu::RunVerdict::Ok;
        analog.attributes = {{"ulk_address","25"}, {"command_v","3.1"},
            {"v7_v","3.100"}, {"reduced_error_percent","-0.03"}};
        yalk.measurements.push_back(analog);

        tu::StepRunResult overload;
        overload.nodeId = "yalk_overload";
        overload.verdict = tu::RunVerdict::Ok;
        tu::MeasurementResult observed;
        observed.parameterKey = "ubsi.yalk.overload.25.26";
        observed.measured = 102.0;
        observed.verdict = tu::RunVerdict::Ok;
        observed.attributes = {{"polarity","+12 В"}, {"stressed_channel","25"},
            {"observed_channel","26"}, {"baseline_code","100"},
            {"current_code","102"}, {"delta_code","2"}};
        overload.measurements.push_back(observed);

        tu::StepRunResult ytp;
        ytp.nodeId = "ytp_channels";
        ytp.verdict = tu::RunVerdict::Ok;
        tu::MeasurementResult thermometer;
        thermometer.parameterKey = "ubsi.ytp.channel.1.1";
        thermometer.reference = 120.0;
        thermometer.measured = 120.25;
        thermometer.unit = "Ом";
        thermometer.verdict = tu::RunVerdict::Ok;
        thermometer.attributes = {{"ytp_channel","1"},
            {"target_resistance_ohm","120"}, {"actual_reference_ohm","120.0"},
            {"reduced_error_percent","0.10"}};
        ytp.measurements.push_back(thermometer);

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
        tu::MeasurementResult afc;
        afc.parameterKey = "ubsi.yvp.afc.3.2";
        afc.reference = 0.0;
        afc.measured = 34.0;
        afc.unit = "%";
        afc.verdict = tu::RunVerdict::Ok;
        afc.attributes = {{"yvp_channel","3"}, {"criterion","afc"},
            {"set_frequency_hz","2"}, {"report_deviation_percent","1.5"},
            {"report_measured_value","1.5"}};
        yvp.measurements.push_back(afc);
        tu::MeasurementResult attenuation;
        attenuation.parameterKey = "ubsi.yvp.attenuation.3.1";
        attenuation.measured = 4.0;
        attenuation.unit = "дБ";
        attenuation.verdict = tu::RunVerdict::Ok;
        attenuation.attributes = {{"yvp_channel","3"}, {"criterion","attenuation"},
            {"report_measured_value","22.7"}};
        yvp.measurements.push_back(attenuation);
        run.steps = {yalk, overload, ytp, yvp};

        const QString htmlPath = writeTuReport(run);
        const QString csvPath = QFileInfo(htmlPath).dir().filePath(
            QFileInfo(htmlPath).completeBaseName() + QStringLiteral(".csv"));
        const QString textPath = QFileInfo(htmlPath).dir().filePath(
            QFileInfo(htmlPath).completeBaseName() + QStringLiteral(".txt"));
        const QString html = readUtf8(htmlPath);
        const QString csv = readUtf8(csvPath);
        const QString protocol = readUtf8(textPath);

        require(protocol.contains(QStringLiteral("ПРОВЕРКА КОНТАКТНЫХ КАНАЛОВ ЯЛК-96")),
                "production TXT does not contain the contact section from the approved layout");
        require(protocol.contains(QStringLiteral("ПРОВЕРКА ОПРОСА И ПРЕОБРАЗОВАНИЯ АНАЛОГОВЫХ СИГНАЛОВ"))
                    && !protocol.contains(QStringLiteral("ПОТЕНЦИАЛЬНЫХ СИГНАЛОВ")),
                "production TXT must use the operator term for YALK analog signals");
        require(protocol.contains(QStringLiteral("Вольтметр: 0.805 В    Состояние: 0")),
                "formal YALK contact value was not normalized");
        require(protocol.contains(QStringLiteral("Код до: 100.00    Код после: 102.00    Δкод: +2.00")),
                "YALK overload per-channel measurements are absent from the TXT layout");
        require(protocol.contains(QStringLiteral("Установлено сопротивление Р4831: 120.00 Ом"))
                    && protocol.contains(QStringLiteral("ЯТП: 120.25 Ом")),
                "YTP point grouping is absent from the TXT layout");
        require(protocol.contains(QStringLiteral("Быстроменяющиеся параметры: максимальное отклонение 2.40 %"))
                    && !protocol.contains(QStringLiteral("372.98 %")),
                "raw YVP deviation leaked into the TU accuracy summary");
        require(!protocol.contains(QStringLiteral("1.1.4.2"))
                    && !protocol.contains(QStringLiteral("НЕ ВЫПОЛНЕНО")),
                "formal TXT must not contain an unperformed sensor-supply section");
        require(protocol.contains(QStringLiteral("Коэффициент усиления 2.00 мВ/пКл = 2.0480 мВ/пКл")),
                "formal YVP value was not normalized");
        require(protocol.contains(QStringLiteral("Частота =  500 Гц    Амплитуда = 1.0000"))
                    && protocol.contains(QStringLiteral("Частота = 4000 Гц    Амплитуда = 0.0733")),
                "YVP AFC reference or attenuation amplitude is missing from the template layout");
        require(protocol.contains(QStringLiteral("Отклонение = 2.4 %")),
                "formal YVP deviation was not normalized");
        require(protocol.contains(QStringLiteral("РЕЗУЛЬТАТЫ ПРОВЕРКИ УБСИ №TEST-001 В НОРМАЛЬНЫХ УСЛОВИЯХ")),
                "production TXT title does not follow the approved layout");
        require(!html.contains(QStringLiteral("9.45963")) && !protocol.contains(QStringLiteral("9.45963")),
                "raw YVP value leaked into the production HTML");
        require(!protocol.contains(QStringLiteral("manual_confirmed"))
                    && !protocol.contains(QStringLiteral("formal_norma"))
                    && !protocol.contains(QStringLiteral("raw_verdict")),
                "engineering verdict policy leaked into the production TXT");
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

        updateTuReportOperator(htmlPath, QStringLiteral("Иванов Иван Иванович"));
        require(readUtf8(textPath).contains(QStringLiteral("Оператор: Иванов Иван Иванович")),
                "operator was not written to the production TXT");
        require(readUtf8(htmlPath).contains(QStringLiteral("Оператор: Иванов Иван Иванович")),
                "operator was not written to the production HTML");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
