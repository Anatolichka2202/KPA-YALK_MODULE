#include "tu_report_writer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>

#include <cmath>
#include <functional>
#include <stdexcept>

namespace {

QString text(const std::string& value)
{
    return QString::fromUtf8(value.c_str());
}

QString protocolVerdict(tu::RunVerdict value)
{
    switch (value) {
    case tu::RunVerdict::Ok: return QStringLiteral("НОРМА");
    case tu::RunVerdict::Fail: return QStringLiteral("НЕ НОРМА");
    case tu::RunVerdict::Error: return QStringLiteral("ОШИБКА СТЕНДА");
    case tu::RunVerdict::Incomplete: return QStringLiteral("НЕ ЗАВЕРШЕНО");
    case tu::RunVerdict::Aborted: return QStringLiteral("ОСТАНОВЛЕНО");
    case tu::RunVerdict::NotRun: return QStringLiteral("НЕ ВЫПОЛНЕНО");
    }
    return QStringLiteral("НЕ ВЫПОЛНЕНО");
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

void visitMeasurements(const std::vector<tu::StepRunResult>& steps,
                       const std::function<void(const tu::StepRunResult&, const tu::MeasurementResult&)>& visitor)
{
    for (const auto& step : steps) {
        for (const auto& measurement : step.measurements) visitor(step, measurement);
        visitMeasurements(step.children, visitor);
    }
}

QString number(double value, int precision = 3)
{
    if (!std::isfinite(value)) return QStringLiteral("—");
    return QLocale::c().toString(value, 'f', precision);
}

void divider(QString& output)
{
    output += QStringLiteral("\n================================================================================================\n\n");
}

void section(QString& output, const QString& title, const QString& tu)
{
    divider(output);
    output += title + QLatin1Char('\n');
    if (!tu.isEmpty()) output += QStringLiteral("(пункт%1 %2 ТУ)\n\n")
        .arg(tu.contains(QLatin1Char(',')) ? QStringLiteral("ы") : QString{}, tu);
}

void sectionResult(QString& output, const QString& title, tu::RunVerdict verdict)
{
    output += QStringLiteral("\n%1    %2\n").arg(title, protocolVerdict(verdict));
}


QString stepVerdict(const tu::StepRunResult& step)
{
    return step.operatorSkipped ? QStringLiteral("ПРОПУЩЕНО") : protocolVerdict(step.verdict);
}

void writeProtocolSteps(QString& output, const std::vector<tu::StepRunResult>& steps)
{
    for (const auto& step : steps) {
        const QString title = text(step.title).toUpper();
        section(output, title, text(step.tuRequirement));
        if (!step.message.empty()) output += text(step.message) + QLatin1Char('\n');
        writeProtocolSteps(output, step.children);
        output += QStringLiteral("\n%1    %2\n").arg(title, stepVerdict(step));
    }
}

QString makeProtocolText(const tu::ScenarioRunResult& result, const QString& operatorName)
{
    const QString title = QStringLiteral("РЕЗУЛЬТАТЫ КАДРОВЫХ ПРОВЕРОК БРП №%1")
        .arg(text(result.objectSerial));
    const auto finished = std::chrono::duration_cast<std::chrono::milliseconds>(
        result.finishedAt.time_since_epoch()).count();
    QString output = title + QStringLiteral("\n\nДата: %1\nОператор: %2\n%3\n")
        .arg(QDateTime::fromMSecsSinceEpoch(finished).toString(QStringLiteral("dd.MM.yyyy")),
            operatorName.trimmed().isEmpty() ? QStringLiteral("не указан") : operatorName.trimmed(),
            text(result.scenarioTitle));

    writeProtocolSteps(output, result.steps);
    divider(output);
    sectionResult(output, QStringLiteral("РЕЗУЛЬТАТЫ ВЫПОЛНЕННЫХ КАДРОВЫХ ПРОВЕРОК БРП"), result.verdict);
    return output;
}

QString attributes(const tu::MeasurementResult& measurement)
{
    QStringList parts;
    for (const auto& [key, value] : measurement.attributes)
        parts << QStringLiteral("%1=%2").arg(text(key), text(value));
    return parts.join(QStringLiteral("; "));
}

} // namespace

QString writeTuReport(const tu::ScenarioRunResult& result, const QString& operatorName)
{
    const QString directory = reportDirectory();
    const QString base = QStringLiteral("TU_%1").arg(safeFilePart(text(result.runId)));
    const QString htmlPath = QDir(directory).filePath(base + QStringLiteral(".html"));
    const QString textPath = QDir(directory).filePath(base + QStringLiteral(".txt"));
    const QString csvPath = QDir(directory).filePath(base + QStringLiteral(".csv"));

    const QString protocol = makeProtocolText(result, operatorName);

    QSaveFile textFile(textPath);
    if (!textFile.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error(QStringLiteral("Не удалось открыть текстовый отчёт: %1")
            .arg(textPath).toUtf8().toStdString());
    const auto bytes = protocol.toUtf8();
    if (textFile.write(bytes) != bytes.size() || !textFile.commit())
        throw std::runtime_error(QStringLiteral("Не удалось сохранить текстовый отчёт: %1")
            .arg(textPath).toUtf8().toStdString());

    QSaveFile htmlFile(htmlPath);
    if (!htmlFile.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error(QStringLiteral("Не удалось открыть отчёт: %1").arg(htmlPath).toUtf8().toStdString());
    QTextStream out(&htmlFile);
    out.setEncoding(QStringConverter::Utf8);
    out << QStringLiteral("<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\"><title>")
        << QStringLiteral("Результаты кадровых проверок БРП").toHtmlEscaped()
        << QStringLiteral("</title><style>@page{size:A4;margin:16mm}body{margin:0;color:#111;font-family:'Courier New',monospace;font-size:10pt;line-height:1.35}pre{margin:0;white-space:pre-wrap;word-break:normal}@media print{body{font-size:9pt}}</style></head><body><pre>")
        << protocol.toHtmlEscaped() << QStringLiteral("</pre></body></html>");
    out.flush();
    if (out.status() != QTextStream::Ok || !htmlFile.commit())
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
            << number(measurement.measured) << QLatin1Char(';')
            << csvCell(text(measurement.unit)) << QLatin1Char(';')
            << number(measurement.lowerLimit) << QLatin1Char(';') << number(measurement.upperLimit) << QLatin1Char(';')
            << csvCell(protocolVerdict(measurement.verdict)) << QLatin1Char(';')
            << csvCell(attributes(measurement)) << QLatin1Char('\n');
    });
    csv.flush();
    if (csv.status() != QTextStream::Ok || !csvFile.commit())
        throw std::runtime_error(QStringLiteral("Не удалось сохранить CSV: %1").arg(csvPath).toUtf8().toStdString());

    return htmlPath;
}
