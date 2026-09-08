#include "ktma/ubsi/production_report.h"

#include <QDateTime>
#include <QDir>
#include <QSaveFile>
#include <QTextStream>

#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ktma::ubsi {
namespace {

QString q(const std::string& value)
{
    return QString::fromUtf8(value);
}

QString html(const std::string& value)
{
    return q(value).toHtmlEscaped();
}

QString iso(std::chrono::system_clock::time_point time)
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        time.time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(ms).toString(Qt::ISODateWithMs);
}

QString verdictText(orbita::stand::RunVerdict verdict)
{
    using orbita::stand::RunVerdict;
    switch (verdict) {
    case RunVerdict::Ok: return QStringLiteral("НОРМА");
    case RunVerdict::Fail: return QStringLiteral("НЕ НОРМА");
    case RunVerdict::Error: return QStringLiteral("ОШИБКА СТЕНДА");
    case RunVerdict::Incomplete: return QStringLiteral("НЕПОЛНАЯ ПРОВЕРКА");
    case RunVerdict::Aborted: return QStringLiteral("ОСТАНОВЛЕНО");
    case RunVerdict::NotRun: return QStringLiteral("НЕ ВЫПОЛНЯЛОСЬ");
    }
    return QStringLiteral("ОШИБКА СТЕНДА");
}

QString packageText(ProductionPackage package)
{
    switch (package) {
    case ProductionPackage::FullUbsi: return QStringLiteral("Полная УБСИ");
    case ProductionPackage::PowerConsumption: return QStringLiteral("Питание / потребление");
    case ProductionPackage::Yalk: return QStringLiteral("ЯЛК");
    case ProductionPackage::Ytp: return QStringLiteral("ЯТП");
    case ProductionPackage::Yvp: return QStringLiteral("ЯВП");
    }
    return QStringLiteral("—");
}

QString field(const orbita::stand::MeasurementResult& measurement,
              std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const auto found = measurement.attributes.find(key);
        if (found != measurement.attributes.end() && !found->second.empty())
            return q(found->second);
    }
    return {};
}

QString csvField(QString value)
{
    value.replace('"', QStringLiteral("\"\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}

QString stageText(registrar::Stage stage)
{
    return QString::fromUtf8(registrar::toString(stage));
}

struct Row
{
    const orbita::stand::StepRunResult* step = nullptr;
    const orbita::stand::MeasurementResult* measurement = nullptr;
};

std::vector<Row> rows(const orbita::stand::ScenarioRunResult& run)
{
    std::vector<Row> result;
    std::function<void(const orbita::stand::StepRunResult&)> collect;
    collect = [&](const orbita::stand::StepRunResult& step) {
        for (const auto& measurement : step.measurements)
            result.push_back({&step, &measurement});
        for (const auto& child : step.children) collect(child);
    };
    for (const auto& step : run.steps) collect(step);
    return result;
}

QString channel(const orbita::stand::MeasurementResult& measurement)
{
    QString value = field(measurement,
        {"ytp_channel", "yalk_address", "ulk_address", "physical_channel", "observed_channel"});
    return value.isEmpty() ? QStringLiteral("—") : value;
}

QString point(const orbita::stand::MeasurementResult& measurement)
{
    const QString value = field(measurement,
        {"actual_reference_ohm", "target_resistance_ohm", "command_v",
         "set_frequency_hz", "frequency_hz", "setpoint_v"});
    return value.isEmpty() ? QStringLiteral("—") : value;
}

QString errorValue(const orbita::stand::MeasurementResult& measurement)
{
    const QString value = field(measurement,
        {"absolute_error_ohm", "absolute_error_v", "gain_error_percent",
         "reduced_error_percent", "relative_error_percent", "delta"});
    if (!value.isEmpty()) return value;
    if (std::isfinite(measurement.measured) && std::isfinite(measurement.reference))
        return QString::number(measurement.measured - measurement.reference, 'g', 12);
    return QStringLiteral("—");
}

void commit(QSaveFile& file)
{
    if (!file.commit())
        throw std::runtime_error(file.errorString().toUtf8().toStdString());
}

} // namespace

ProductionRunStatus productionStatusFromScenarioVerdict(
    orbita::stand::RunVerdict verdict) noexcept
{
    using orbita::stand::RunVerdict;
    switch (verdict) {
    case RunVerdict::Ok: return ProductionRunStatus::Norm;
    case RunVerdict::Fail: return ProductionRunStatus::NotNorm;
    case RunVerdict::Error: return ProductionRunStatus::StandError;
    case RunVerdict::Incomplete:
    case RunVerdict::NotRun: return ProductionRunStatus::Incomplete;
    case RunVerdict::Aborted: return ProductionRunStatus::Stopped;
    }
    return ProductionRunStatus::StandError;
}

ProductionReportPaths writeProductionReport(
    const orbita::stand::ScenarioRunResult& run,
    const ProductionRunContext& context,
    const std::string& directoryPath)
{
    QDir directory(QString::fromUtf8(directoryPath));
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
        throw std::runtime_error("cannot create UBSI production report directory");

    const QString runId = q(run.runId);
    const QString stem = QStringLiteral("Производственный_отчет_%1").arg(runId);
    const QString htmlPath = directory.filePath(stem + QStringLiteral(".html"));
    const QString csvPath = directory.filePath(stem + QStringLiteral(".csv"));
    const auto measurements = rows(run);

    QSaveFile csv(csvPath);
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error(csv.errorString().toUtf8().toStdString());
    csv.write("\xEF\xBB\xBF");
    QTextStream csvOut(&csv);
    csvOut.setEncoding(QStringConverter::Utf8);
    csvOut << QStringLiteral("Изделие;Этап;Пакет;Тест;Канал/адрес;Точка;Задано;Измерено;Ед.;Ошибка;Нижний допуск;Верхний допуск;Итог;Попытка;Ошибка стенда;run_id\n");
    for (const auto& row : measurements) {
        const auto& m = *row.measurement;
        const QString attempt = field(m, {"attempt", "attempt_index", "retry_index"});
        const QString standError = m.verdict == orbita::stand::RunVerdict::Error
            ? q(m.message) : QString();
        csvOut << csvField(q(context.productSerial)) << ';'
               << csvField(stageText(context.stage)) << ';'
               << csvField(packageText(context.package)) << ';'
               << csvField(q(row.step->title)) << ';'
               << csvField(channel(m)) << ';'
               << csvField(point(m)) << ';'
               << csvField(QString::number(m.reference, 'g', 12)) << ';'
               << csvField(QString::number(m.measured, 'g', 12)) << ';'
               << csvField(q(m.unit)) << ';'
               << csvField(errorValue(m)) << ';'
               << csvField(QString::number(m.lowerLimit, 'g', 12)) << ';'
               << csvField(QString::number(m.upperLimit, 'g', 12)) << ';'
               << csvField(verdictText(m.verdict)) << ';'
               << csvField(attempt.isEmpty() ? QStringLiteral("—") : attempt) << ';'
               << csvField(standError) << ';'
               << csvField(runId) << '\n';
    }
    csvOut.flush();
    commit(csv);

    QSaveFile report(htmlPath);
    if (!report.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error(report.errorString().toUtf8().toStdString());
    QTextStream out(&report);
    out.setEncoding(QStringConverter::Utf8);
    out << QStringLiteral("<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\"><title>Производственный отчёт УБСИ</title><style>")
        << QStringLiteral("body{font:13px 'Segoe UI',sans-serif;margin:24px;color:#111}h1{margin-bottom:4px}table{border-collapse:collapse;width:100%;margin:12px 0}th,td{border:1px solid #777;padding:6px 8px;text-align:left}th{background:#eee}.meta{max-width:900px}.FAIL,.ERROR{font-weight:700}.muted{color:#555}@media print{body{margin:8mm}}</style></head><body>")
        << QStringLiteral("<h1>Производственный отчёт УБСИ</h1><p class=\"muted\">run_id: ")
        << runId.toHtmlEscaped() << QStringLiteral("</p><table class=\"meta\"><tbody>")
        << QStringLiteral("<tr><th>Изделие</th><td>") << html(context.productSerial) << QStringLiteral("</td></tr>")
        << QStringLiteral("<tr><th>Этап</th><td>") << stageText(context.stage).toHtmlEscaped() << QStringLiteral("</td></tr>")
        << QStringLiteral("<tr><th>Пакет</th><td>") << packageText(context.package).toHtmlEscaped() << QStringLiteral("</td></tr>")
        << QStringLiteral("<tr><th>Сценарий</th><td>") << html(run.scenarioId) << QStringLiteral(" · ") << html(run.scenarioVersion) << QStringLiteral("</td></tr>")
        << QStringLiteral("<tr><th>Начало</th><td>") << iso(run.startedAt) << QStringLiteral("</td></tr>")
        << QStringLiteral("<tr><th>Окончание</th><td>") << iso(run.finishedAt) << QStringLiteral("</td></tr>")
        << QStringLiteral("<tr><th>Итог</th><td><b>") << verdictText(run.verdict) << QStringLiteral("</b></td></tr></tbody></table>");

    out << QStringLiteral("<h2>Состав изделия на момент запуска</h2><table><thead><tr><th>Ячейка</th><th>SN</th><th>Входит в пакет</th></tr></thead><tbody>");
    for (const auto& component : context.composition) {
        out << QStringLiteral("<tr><td>") << html(component.componentType)
            << QStringLiteral("</td><td>") << html(component.serialNumber)
            << QStringLiteral("</td><td>")
            << (component.affected ? QStringLiteral("ДА") : QStringLiteral("НЕТ"))
            << QStringLiteral("</td></tr>");
    }
    out << QStringLiteral("</tbody></table><h2>Измерения</h2><table><thead><tr><th>Тест</th><th>Канал/адрес</th><th>Точка</th><th>Задано</th><th>Измерено</th><th>Ед.</th><th>Ошибка</th><th>Допуск</th><th>Итог</th><th>Попытка</th></tr></thead><tbody>");
    for (const auto& row : measurements) {
        const auto& m = *row.measurement;
        const QString attempt = field(m, {"attempt", "attempt_index", "retry_index"});
        out << QStringLiteral("<tr class=\"") << q(orbita::stand::toString(m.verdict))
            << QStringLiteral("\"><td>") << html(row.step->title)
            << QStringLiteral("</td><td>") << channel(m).toHtmlEscaped()
            << QStringLiteral("</td><td>") << point(m).toHtmlEscaped()
            << QStringLiteral("</td><td>") << QString::number(m.reference, 'g', 12)
            << QStringLiteral("</td><td>") << QString::number(m.measured, 'g', 12)
            << QStringLiteral("</td><td>") << html(m.unit)
            << QStringLiteral("</td><td>") << errorValue(m).toHtmlEscaped()
            << QStringLiteral("</td><td>") << QString::number(m.lowerLimit, 'g', 12)
            << QStringLiteral(" … ") << QString::number(m.upperLimit, 'g', 12)
            << QStringLiteral("</td><td>") << verdictText(m.verdict)
            << QStringLiteral("</td><td>") << (attempt.isEmpty() ? QStringLiteral("—") : attempt.toHtmlEscaped())
            << QStringLiteral("</td></tr>");
    }
    out << QStringLiteral("</tbody></table><h2>Журнал выполнения</h2><table><thead><tr><th>Время</th><th>Этап</th><th>Событие</th><th>Сообщение</th></tr></thead><tbody>");
    for (const auto& event : run.events) {
        out << QStringLiteral("<tr><td>") << iso(event.timestamp)
            << QStringLiteral("</td><td>") << html(event.nodeId)
            << QStringLiteral("</td><td>") << html(event.stage)
            << QStringLiteral("</td><td>") << html(event.message)
            << QStringLiteral("</td></tr>");
    }
    out << QStringLiteral("</tbody></table></body></html>");
    out.flush();
    commit(report);

    return {htmlPath.toUtf8().toStdString(), csvPath.toUtf8().toStdString()};
}

} // namespace ktma::ubsi
