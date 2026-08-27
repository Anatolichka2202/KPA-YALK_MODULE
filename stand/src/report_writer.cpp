#include "orbita_stand/report_writer.h"

#include <QDateTime>
#include <QDir>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace orbita::stand {
namespace {

QString escape(const std::string& value)
{
    return QString::fromUtf8(value).toHtmlEscaped();
}

QString raw(const std::string& value)
{
    return QString::fromUtf8(value);
}

QString localVerdict(RunVerdict verdict)
{
    switch (verdict) {
    case RunVerdict::Ok: return QStringLiteral("ОК");
    case RunVerdict::Fail: return QStringLiteral("НЕ ОК");
    case RunVerdict::Incomplete: return QStringLiteral("НЕПОЛНАЯ");
    case RunVerdict::Error: return QStringLiteral("ОШИБКА");
    case RunVerdict::Aborted: return QStringLiteral("ОСТАНОВЛЕНО");
    case RunVerdict::NotRun: return QStringLiteral("НЕ ВЫПОЛНЯЛОСЬ");
    }
    return QStringLiteral("ОШИБКА");
}

QString iso(std::chrono::system_clock::time_point time)
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(ms).toString(Qt::ISODateWithMs);
}

void commit(QSaveFile& file)
{
    if (!file.commit()) throw std::runtime_error(file.errorString().toUtf8().toStdString());
}

} // namespace

ReportPaths writeHtmlCsvReport(const ScenarioRunResult& run, const std::string& directoryPath)
{
    QDir directory(QString::fromUtf8(directoryPath));
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        throw std::runtime_error("Cannot create report directory");
    }
    const QString stem = QStringLiteral("Испытание_%1").arg(QString::fromUtf8(run.runId));
    const QString htmlPath = directory.filePath(stem + QStringLiteral(".html"));
    const QString csvPath = directory.filePath(stem + QStringLiteral(".csv"));

    std::vector<std::pair<const StepRunResult*, const MeasurementResult*>> measurements;
    std::function<void(const StepRunResult&)> collect = [&](const StepRunResult& step) {
        for (const auto& value : step.measurements) measurements.push_back({&step, &value});
        for (const auto& child : step.children) collect(child);
    };
    for (const auto& step : run.steps) collect(step);

    QSaveFile csv(csvPath);
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Text)) throw std::runtime_error(csv.errorString().toUtf8().toStdString());
    csv.write("\xEF\xBB\xBF");
    QTextStream csvStream(&csv);
    csvStream.setEncoding(QStringConverter::Utf8);
    csvStream << QStringLiteral("Этап;Пункт ТУ;Параметр;Эталон;Измерено;Нижний допуск;Верхний допуск;Единица;Итог;Сообщение\n");
    const auto field = [](QString value) { return QStringLiteral("\"") + value.replace('"', QStringLiteral("\"\"")) + QStringLiteral("\""); };
    for (const auto& [step, value] : measurements) {
        csvStream << field(raw(step->title)) << ';' << field(raw(step->tuRequirement)) << ';'
                  << field(raw(value->title)) << ';' << QString::number(value->reference, 'g', 15) << ';'
                  << QString::number(value->measured, 'g', 15) << ';' << QString::number(value->lowerLimit, 'g', 15) << ';'
                  << QString::number(value->upperLimit, 'g', 15) << ';' << field(raw(value->unit)) << ';'
                  << field(localVerdict(value->verdict)) << ';' << field(raw(value->message)) << '\n';
    }
    csvStream.flush();
    commit(csv);

    QSaveFile html(htmlPath);
    if (!html.open(QIODevice::WriteOnly | QIODevice::Text)) throw std::runtime_error(html.errorString().toUtf8().toStdString());
    QTextStream output(&html);
    output.setEncoding(QStringConverter::Utf8);
    const QString scenarioTitle = run.scenarioTitle.empty()
        ? raw(run.scenarioId) : raw(run.scenarioTitle);
    output << QStringLiteral("<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\"><title>")
           << scenarioTitle.toHtmlEscaped() << QStringLiteral("</title><style>")
           << QStringLiteral("body{font:14px 'Segoe UI',sans-serif;color:#17202a;margin:32px}h1{margin-bottom:4px}.summary{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}.card{border:1px solid #ccd3da;padding:12px;border-radius:6px}.verdict{font-size:24px;font-weight:700}.OK{color:#167548}.FAIL,.ERROR{color:#b3261e}.INCOMPLETE,.ABORTED{color:#9a6700}.chart{border:1px solid #ccd3da;margin-top:18px;padding:10px}table{width:100%;border-collapse:collapse;margin-top:18px}th,td{border:1px solid #ccd3da;padding:6px 8px;text-align:left}th{background:#edf1f5}tr.FAIL,tr.ERROR{background:#fff0ef}tr.INCOMPLETE{background:#fff8df}.num{text-align:right;font-variant-numeric:tabular-nums}@media print{body{margin:10mm}.no-print{display:none}}</style></head><body>")
           << QStringLiteral("<h1>") << scenarioTitle.toHtmlEscaped()
           << QStringLiteral("</h1><p>") << escape(run.scenarioId) << QStringLiteral(" · версия ")
           << escape(run.scenarioVersion) << QStringLiteral("</p><div class=\"summary\"><div class=\"card\"><div>Итог</div><div class=\"verdict ")
           << QString::fromLatin1(toString(run.verdict)) << QStringLiteral("\">") << localVerdict(run.verdict)
           << QStringLiteral("</div></div><div class=\"card\"><div>Начало</div><b>") << iso(run.startedAt)
           << QStringLiteral("</b><div>Окончание</div><b>") << iso(run.finishedAt)
           << QStringLiteral("</b></div><div class=\"card\"><div>Серийный номер</div><b>")
           << (run.objectSerial.empty() ? QStringLiteral("не указан") : escape(run.objectSerial))
           << QStringLiteral("</b><div>Каталог / профиль</div><b>") << escape(run.catalogVersion) << QStringLiteral(" / ")
           << escape(run.profileVersion) << QStringLiteral("</b></div></div>");

    if (!measurements.empty()) {
        double minimum = 0.0;
        double maximum = 0.0;
        for (const auto& [step, value] : measurements) {
            Q_UNUSED(step);
            minimum = std::min({minimum, value->reference, value->measured, value->lowerLimit});
            maximum = std::max({maximum, value->reference, value->measured, value->upperLimit});
        }
        if (maximum <= minimum) maximum = minimum + 1.0;
        const double width = 860.0;
        const double height = 220.0;
        const double left = 30.0;
        const double top = 15.0;
        auto point = [&](std::size_t index, double value) {
            const double x = left + (measurements.size() == 1 ? width / 2.0
                : width * static_cast<double>(index) / static_cast<double>(measurements.size() - 1));
            const double y = top + height - (value - minimum) * height / (maximum - minimum);
            return QStringLiteral("%1,%2").arg(x, 0, 'f', 1).arg(y, 0, 'f', 1);
        };
        QString referencePoints;
        QString measuredPoints;
        for (std::size_t index = 0; index < measurements.size(); ++index) {
            if (index) { referencePoints += ' '; measuredPoints += ' '; }
            referencePoints += point(index, measurements[index].second->reference);
            measuredPoints += point(index, measurements[index].second->measured);
        }
        output << QStringLiteral("<h2>График измерений</h2><div class=\"chart\"><svg viewBox=\"0 0 920 270\" role=\"img\" aria-label=\"Эталонные и измеренные значения\"><line x1=\"30\" y1=\"235\" x2=\"890\" y2=\"235\" stroke=\"#8b949e\"/><line x1=\"30\" y1=\"15\" x2=\"30\" y2=\"235\" stroke=\"#8b949e\"/><polyline fill=\"none\" stroke=\"#1f6feb\" stroke-width=\"2\" points=\"")
               << referencePoints << QStringLiteral("\"/><polyline fill=\"none\" stroke=\"#238636\" stroke-width=\"2\" points=\"")
               << measuredPoints << QStringLiteral("\"/><text x=\"40\" y=\"260\" fill=\"#1f6feb\">— эталон</text><text x=\"150\" y=\"260\" fill=\"#238636\">— измерено</text></svg></div>");
    }

    output << QStringLiteral("<table><thead><tr><th>Этап</th><th>ТУ</th><th>Параметр</th><th>Эталон</th><th>Измерено</th><th>Допуск</th><th>Итог</th></tr></thead><tbody>");
    for (const auto& [step, value] : measurements) {
        output << QStringLiteral("<tr class=\"") << QString::fromLatin1(toString(value->verdict)) << QStringLiteral("\"><td>")
               << escape(step->title) << QStringLiteral("</td><td>") << escape(step->tuRequirement)
               << QStringLiteral("</td><td>") << escape(value->title) << QStringLiteral("</td><td class=\"num\">")
               << QString::number(value->reference, 'g', 10) << QStringLiteral(" ") << escape(value->unit)
               << QStringLiteral("</td><td class=\"num\">") << QString::number(value->measured, 'g', 10)
               << QStringLiteral(" ") << escape(value->unit) << QStringLiteral("</td><td class=\"num\">")
               << QString::number(value->lowerLimit, 'g', 10) << QStringLiteral(" … ")
               << QString::number(value->upperLimit, 'g', 10) << QStringLiteral("</td><td>")
               << localVerdict(value->verdict) << QStringLiteral("</td></tr>");
    }
    output << QStringLiteral("</tbody></table><h2>Журнал этапов</h2><table><thead><tr><th>Время</th><th>Этап</th><th>Событие</th><th>Сообщение</th></tr></thead><tbody>");
    for (const auto& event : run.events) {
        output << QStringLiteral("<tr><td>") << iso(event.timestamp) << QStringLiteral("</td><td>") << escape(event.nodeId)
               << QStringLiteral("</td><td>") << escape(event.stage) << QStringLiteral("</td><td>") << escape(event.message)
               << QStringLiteral("</td></tr>");
    }
    output << QStringLiteral("</tbody></table><p class=\"no-print\"><a href=\"") << stem
           << QStringLiteral(".csv\">Скачать таблицу CSV</a></p></body></html>");
    output.flush();
    commit(html);
    return {htmlPath.toUtf8().toStdString(), csvPath.toUtf8().toStdString()};
}

} // namespace orbita::stand
