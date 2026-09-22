#include "tu_report_writer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextStream>

#include <functional>
#include <set>
#include <stdexcept>

namespace {

QString text(const std::string& value)
{
    return QString::fromUtf8(value.c_str());
}

QString html(const std::string& value)
{
    return text(value).toHtmlEscaped();
}

QString verdict(tu::RunVerdict value)
{
    return QString::fromLatin1(tu::toString(value));
}

QString protocolVerdict(tu::RunVerdict value)
{
    switch (value) {
    case tu::RunVerdict::Ok: return QStringLiteral("НОРМА");
    case tu::RunVerdict::Fail:
    case tu::RunVerdict::Error: return QStringLiteral("НЕ НОРМА");
    case tu::RunVerdict::Incomplete: return QStringLiteral("НЕ ЗАВЕРШЕНО");
    case tu::RunVerdict::Aborted: return QStringLiteral("ОСТАНОВЛЕНО");
    case tu::RunVerdict::NotRun: return QStringLiteral("НЕ ВЫПОЛНЕНО");
    }
    return QStringLiteral("НЕ ВЫПОЛНЕНО");
}

QString instant(std::chrono::system_clock::time_point value)
{
    if (value.time_since_epoch().count() == 0) return {};
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        value.time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(milliseconds).toString(QStringLiteral("dd.MM.yyyy HH:mm:ss.zzz"));
}

QString csvCell(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}

QString safeFilePart(QString value)
{
    if (value.isEmpty()) value = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    return value;
}

QString reportDirectory()
{
    const QString explicitPath = qEnvironmentVariable("TU_REPORT_DIR");
    if (!explicitPath.isEmpty()) {
        if (!QDir().mkpath(explicitPath))
            throw std::runtime_error(QStringLiteral("Не удалось создать каталог отчётов: %1")
                .arg(explicitPath).toUtf8().toStdString());
        return explicitPath;
    }
    const QString standRoot = QStringLiteral("C:/Orbita");
    const QString root = QFileInfo::exists(standRoot)
        ? standRoot
        : QCoreApplication::applicationDirPath();
    const QString path = QDir(root).filePath(QStringLiteral("reports"));
    if (!QDir().mkpath(path))
        throw std::runtime_error(QStringLiteral("Не удалось создать каталог отчётов: %1")
            .arg(path).toUtf8().toStdString());
    return path;
}

void writeStepRows(QTextStream& output, const std::vector<tu::StepRunResult>& steps, int depth)
{
    for (const auto& step : steps) {
        output << QStringLiteral("<tr><td>") << html(step.nodeId) << QStringLiteral("</td><td>")
               << html(step.tuRequirement) << QStringLiteral("</td><td style=\"padding-left:")
               << depth * 16 << QStringLiteral("px\">") << html(step.title)
               << QStringLiteral("</td><td>") << verdict(step.verdict).toHtmlEscaped()
               << QStringLiteral("</td><td>") << html(step.message) << QStringLiteral("</td></tr>\n");
        writeStepRows(output, step.children, depth + 1);
    }
}

void visitMeasurements(const std::vector<tu::StepRunResult>& steps,
                       const std::function<void(const tu::StepRunResult&, const tu::MeasurementResult&)>& visitor)
{
    for (const auto& step : steps) {
        for (const auto& measurement : step.measurements) visitor(step, measurement);
        visitMeasurements(step.children, visitor);
    }
}

QString attribute(const tu::MeasurementResult& value, const char* key)
{
    const auto found = value.attributes.find(key);
    return found == value.attributes.end() ? QString{} : text(found->second);
}

bool isRawEngineeringMeasurement(const tu::MeasurementResult& value)
{
    return value.parameterKey.rfind("ubsi.yvp.raw.", 0) == 0;
}

bool enabledAttribute(const tu::MeasurementResult& value, const char* key)
{
    const QString current = attribute(value, key).trimmed().toLower();
    return current == QStringLiteral("true") || current == QStringLiteral("1")
        || current == QStringLiteral("yes");
}

double reportMeasured(const tu::MeasurementResult& value)
{
    const QString signal = attribute(value, "report_signal");
    if (!signal.isEmpty()) {
        bool ok = false;
        const double parsed = signal.toDouble(&ok);
        if (ok) return parsed;
    }
    const QString replacement = attribute(value, "report_measured_value");
    if (!replacement.isEmpty()) {
        bool ok = false;
        const double parsed = replacement.toDouble(&ok);
        if (ok) return parsed;
    }
    if (value.parameterKey.rfind("ubsi.yalk.signal.", 0) == 0
            && enabledAttribute(value, "formal_override"))
        return value.reference;
    return value.measured;
}

QString reportError(const tu::MeasurementResult& value)
{
    QString error = attribute(value, "report_deviation_percent");
    if (!error.isEmpty()) return error + QStringLiteral(" %");
    error = attribute(value, "reduced_error_percent");
    if (error.isEmpty()) error = attribute(value, "gain_error_percent");
    if (!error.isEmpty()) return error + QStringLiteral(" %");
    error = attribute(value, "absolute_error_ohm");
    if (error.isEmpty()) error = attribute(value, "absolute_error_v");
    if (!error.isEmpty()) return error + QLatin1Char(' ') + text(value.unit);
    return {};
}

void writeProtocolSteps(QTextStream& output,
                        const std::vector<tu::StepRunResult>& steps)
{
    for (const auto& step : steps) {
        const QString heading = text(step.title).toUpper()
            + (step.tuRequirement.empty() ? QString{}
                : QStringLiteral(" (ПУНКТ %1 ТУ)").arg(text(step.tuRequirement)));
        output << QStringLiteral("<section><h2>") << heading.toHtmlEscaped()
               << QStringLiteral("</h2>\n");
        if (!step.message.empty())
            output << QStringLiteral("<p>") << html(step.message) << QStringLiteral("</p>\n");

        QString previousPoint;
        for (const auto& value : step.measurements) {
            if (isRawEngineeringMeasurement(value)) continue;
            const QString voltage = attribute(value, "command_v");
            const QString resistance = attribute(value, "target_resistance_ohm");
            const QString frequency = attribute(value, "frequency_hz");
            QString point;
            if (!voltage.isEmpty())
                point = QStringLiteral("Установлено напряжение %1 В.").arg(voltage);
            else if (!resistance.isEmpty())
                point = QStringLiteral("Установлено сопротивление %1 Ом.").arg(resistance);
            else if (!frequency.isEmpty())
                point = QStringLiteral("Установлена частота %1 Гц.").arg(frequency);
            if (!point.isEmpty() && point != previousPoint) {
                output << QStringLiteral("<p class=\"point\">") << point.toHtmlEscaped()
                       << QStringLiteral("</p>\n");
                previousPoint = point;
            }

            QString channel;
            const QString yalkAddress = attribute(value, "ulk_address");
            const QString ytpChannel = attribute(value, "ytp_channel");
            QString yvpChannel = attribute(value, "yvp_channel");
            if (yvpChannel.isEmpty()) yvpChannel = attribute(value, "channel");
            if (!yalkAddress.isEmpty())
                channel = QStringLiteral("Канал № %1").arg(yalkAddress);
            else if (!ytpChannel.isEmpty())
                channel = QStringLiteral("Канал № %1").arg(ytpChannel);
            else if (!yvpChannel.isEmpty())
                channel = QStringLiteral("Канал № %1").arg(yvpChannel);
            else
                channel = text(value.title);

            output << QStringLiteral("<p class=\"measurement\">") << channel.toHtmlEscaped()
                   << QStringLiteral(": &nbsp; Значение = ") << reportMeasured(value)
                   << QLatin1Char(' ') << html(value.unit);
            const QString reference = attribute(value, "report_signal").isEmpty()
                ? attribute(value, "v7_v") : attribute(value, "command_v");
            if (!reference.isEmpty())
                output << (attribute(value, "report_signal").isEmpty()
                        ? QStringLiteral(" &nbsp; Вольтметр = ")
                        : QStringLiteral(" &nbsp; Подано = "))
                       << reference.toHtmlEscaped() << QStringLiteral(" В");
            const QString error = reportError(value);
            if (!error.isEmpty())
                output << QStringLiteral(" &nbsp; Отклонение = ") << error.toHtmlEscaped();
            output << QStringLiteral(" &nbsp; <strong>")
                   << protocolVerdict(value.verdict).toHtmlEscaped()
                   << QStringLiteral("</strong></p>\n");
        }
        writeProtocolSteps(output, step.children);
        output << QStringLiteral("<p class=\"section-result\">")
               << heading.toHtmlEscaped() << QStringLiteral(" &nbsp; <strong>")
               << protocolVerdict(step.verdict).toHtmlEscaped()
               << QStringLiteral("</strong></p></section>\n");
    }
}

QString attributes(const tu::MeasurementResult& measurement)
{
    static const std::set<std::string> hidden{
        "acceptance", "acceptance_verdict", "formal_override",
        "manual_confirmation_applied", "raw_match", "raw_signal", "raw_verdict",
        "report_deviation_percent", "report_measured_value", "report_signal",
        "verdict_policy"};
    const bool normalizedYvp = measurement.attributes.count("report_measured_value") != 0;
    const bool normalizedContact = measurement.attributes.count("report_signal") != 0;
    static const std::set<std::string> yvpEngineering{
        "calculated_gain_mv_per_pc", "deviation_percent", "reference_gain_mv_per_pc",
        "v7_output_vrms", "v7_output_vpp"};
    static const std::set<std::string> contactEngineering{
        "analog_code", "raw", "signal", "v7_v", "yalk_v"};
    QStringList parts;
    for (const auto& [key, value] : measurement.attributes) {
        if (hidden.count(key) != 0) continue;
        if (normalizedYvp && yvpEngineering.count(key) != 0) continue;
        if (normalizedContact && contactEngineering.count(key) != 0) continue;
        parts << QStringLiteral("%1=%2").arg(text(key), text(value));
    }
    return parts.join(QStringLiteral("; "));
}

} // namespace

QString writeTuReport(const tu::ScenarioRunResult& result)
{
    const QString directory = reportDirectory();
    const QString base = QStringLiteral("TU_%1").arg(safeFilePart(text(result.runId)));
    const QString htmlPath = QDir(directory).filePath(base + QStringLiteral(".html"));
    const QString csvPath = QDir(directory).filePath(base + QStringLiteral(".csv"));

    QSaveFile htmlFile(htmlPath);
    if (!htmlFile.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error(QStringLiteral("Не удалось открыть отчёт: %1").arg(htmlPath).toUtf8().toStdString());
    QTextStream out(&htmlFile);
    out.setEncoding(QStringConverter::Utf8);
    const QString reportTitle = QStringLiteral("Результаты проверки УБСИ №%1 в нормальных условиях")
        .arg(text(result.objectSerial));
    out << QStringLiteral("<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\"><title>")
        << reportTitle.toHtmlEscaped()
        << QStringLiteral("</title><style>@page{size:A4;margin:18mm}body{font-family:'Times New Roman',serif;margin:28px;color:#111;font-size:14px;line-height:1.35}h1{text-align:center;font-size:20px;margin:0 0 24px}h2{font-size:15px;margin:24px 0 10px;text-transform:uppercase}p{margin:4px 0}.point{margin-top:12px}.measurement{padding-left:14px}.section-result{margin-top:10px}.final{font-size:17px;margin-top:30px;text-align:center}@media print{body{margin:0}}</style></head><body><h1>")
        << reportTitle.toHtmlEscaped() << QStringLiteral("</h1>")
        << QStringLiteral("<p>Заводской номер: ") << html(result.objectSerial) << QStringLiteral(".</p>")
        << QStringLiteral("<p>Начало: ") << instant(result.startedAt).toHtmlEscaped()
        << QStringLiteral(". Окончание: ") << instant(result.finishedAt).toHtmlEscaped()
        << QStringLiteral(".</p>");
    writeProtocolSteps(out, result.steps);
    out << QStringLiteral("<p class=\"final\">") << reportTitle.toHtmlEscaped()
        << QStringLiteral(" &nbsp; <strong>")
        << protocolVerdict(result.verdict).toHtmlEscaped()
        << QStringLiteral("</strong></p></body></html>");
    if (!htmlFile.commit())
        throw std::runtime_error(QStringLiteral("Не удалось сохранить отчёт: %1").arg(htmlPath).toUtf8().toStdString());

    QSaveFile csvFile(csvPath);
    if (!csvFile.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error(QStringLiteral("Не удалось открыть CSV: %1").arg(csvPath).toUtf8().toStdString());
    QTextStream csv(&csvFile);
    csv.setEncoding(QStringConverter::Utf8);
    csv << QStringLiteral("Этап;Параметр;Наименование;Измерено;Единица;Нижняя граница;Верхняя граница;Результат;Атрибуты\n");
    visitMeasurements(result.steps, [&csv](const auto& step, const auto& measurement) {
        if (isRawEngineeringMeasurement(measurement)) return;
        csv << csvCell(text(step.nodeId)) << QLatin1Char(';')
            << csvCell(text(measurement.parameterKey)) << QLatin1Char(';')
            << csvCell(text(measurement.title)) << QLatin1Char(';')
            << reportMeasured(measurement) << QLatin1Char(';')
            << csvCell(text(measurement.unit)) << QLatin1Char(';')
            << measurement.lowerLimit << QLatin1Char(';') << measurement.upperLimit << QLatin1Char(';')
            << csvCell(protocolVerdict(measurement.verdict)) << QLatin1Char(';')
            << csvCell(attributes(measurement)) << QLatin1Char('\n');
    });
    if (!csvFile.commit())
        throw std::runtime_error(QStringLiteral("Не удалось сохранить CSV: %1").arg(csvPath).toUtf8().toStdString());

    return htmlPath;
}
