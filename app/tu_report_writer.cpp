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

QString attributes(const std::map<std::string, std::string>& values)
{
    QStringList parts;
    for (const auto& [key, value] : values)
        parts << QStringLiteral("%1=%2").arg(text(key), text(value));
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
    out << QStringLiteral("<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\"><title>Протокол ТУ</title>")
        << QStringLiteral("<style>body{font-family:Arial,sans-serif;margin:24px;color:#17202a}table{border-collapse:collapse;width:100%;margin:12px 0 28px}th,td{border:1px solid #aeb6bf;padding:6px;text-align:left;vertical-align:top}th{background:#edf2f7}h1,h2{margin-bottom:8px}</style></head><body>")
        << QStringLiteral("<h1>Протокол проверки УБСИ по ТУ</h1><table>")
        << QStringLiteral("<tr><th>Запуск</th><td>") << html(result.runId) << QStringLiteral("</td></tr>")
        << QStringLiteral("<tr><th>Сценарий</th><td>") << html(result.scenarioTitle) << QStringLiteral(" · ")
        << html(result.scenarioVersion) << QStringLiteral("</td></tr>")
        << QStringLiteral("<tr><th>Заводской номер</th><td>") << html(result.objectSerial) << QStringLiteral("</td></tr>")
        << QStringLiteral("<tr><th>Оператор</th><td></td></tr>")
        << QStringLiteral("<tr><th>Начало</th><td>") << instant(result.startedAt).toHtmlEscaped() << QStringLiteral("</td></tr>")
        << QStringLiteral("<tr><th>Окончание</th><td>") << instant(result.finishedAt).toHtmlEscaped() << QStringLiteral("</td></tr>")
        << QStringLiteral("<tr><th>Итог</th><td>") << verdict(result.verdict).toHtmlEscaped() << QStringLiteral("</td></tr></table>")
        << QStringLiteral("<h2>Этапы</h2><table><tr><th>ID</th><th>ТУ</th><th>Этап</th><th>Результат</th><th>Сообщение</th></tr>");
    writeStepRows(out, result.steps, 0);
    out << QStringLiteral("</table><h2>Измерения</h2><table><tr><th>Этап</th><th>Параметр</th><th>Наименование</th><th>Измерено</th><th>Норма</th><th>Результат</th><th>Атрибуты</th></tr>");
    visitMeasurements(result.steps, [&out](const auto& step, const auto& measurement) {
        out << QStringLiteral("<tr><td>") << html(step.nodeId) << QStringLiteral("</td><td>")
            << html(measurement.parameterKey) << QStringLiteral("</td><td>") << html(measurement.title)
            << QStringLiteral("</td><td>") << measurement.measured << QLatin1Char(' ') << html(measurement.unit)
            << QStringLiteral("</td><td>") << measurement.lowerLimit << QStringLiteral(" … ")
            << measurement.upperLimit << QLatin1Char(' ') << html(measurement.unit)
            << QStringLiteral("</td><td>") << verdict(measurement.verdict).toHtmlEscaped()
            << QStringLiteral("</td><td>") << attributes(measurement.attributes).toHtmlEscaped()
            << QStringLiteral("</td></tr>\n");
    });
    out << QStringLiteral("</table><h2>Журнал команд и событий</h2><table><tr><th>Время</th><th>Этап</th><th>Событие</th><th>Сообщение</th><th>Результат</th><th>Данные</th></tr>");
    for (const auto& event : result.events) {
        out << QStringLiteral("<tr><td>") << instant(event.timestamp).toHtmlEscaped()
            << QStringLiteral("</td><td>") << html(event.nodeId)
            << QStringLiteral("</td><td>") << html(event.stage)
            << QStringLiteral("</td><td>") << html(event.message)
            << QStringLiteral("</td><td>") << verdict(event.verdict).toHtmlEscaped()
            << QStringLiteral("</td><td>") << attributes(event.data).toHtmlEscaped()
            << QStringLiteral("</td></tr>\n");
    }
    out << QStringLiteral("</table></body></html>");
    if (!htmlFile.commit())
        throw std::runtime_error(QStringLiteral("Не удалось сохранить отчёт: %1").arg(htmlPath).toUtf8().toStdString());

    QSaveFile csvFile(csvPath);
    if (!csvFile.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error(QStringLiteral("Не удалось открыть CSV: %1").arg(csvPath).toUtf8().toStdString());
    QTextStream csv(&csvFile);
    csv.setEncoding(QStringConverter::Utf8);
    csv << QStringLiteral("Этап;Параметр;Наименование;Измерено;Единица;Нижняя граница;Верхняя граница;Результат;Атрибуты\n");
    visitMeasurements(result.steps, [&csv](const auto& step, const auto& measurement) {
        csv << csvCell(text(step.nodeId)) << QLatin1Char(';')
            << csvCell(text(measurement.parameterKey)) << QLatin1Char(';')
            << csvCell(text(measurement.title)) << QLatin1Char(';')
            << measurement.measured << QLatin1Char(';')
            << csvCell(text(measurement.unit)) << QLatin1Char(';')
            << measurement.lowerLimit << QLatin1Char(';') << measurement.upperLimit << QLatin1Char(';')
            << csvCell(verdict(measurement.verdict)) << QLatin1Char(';')
            << csvCell(attributes(measurement.attributes)) << QLatin1Char('\n');
    });
    if (!csvFile.commit())
        throw std::runtime_error(QStringLiteral("Не удалось сохранить CSV: %1").arg(csvPath).toUtf8().toStdString());

    return htmlPath;
}
