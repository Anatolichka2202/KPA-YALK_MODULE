#include "orbita_stand/report_writer.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <functional>
#include <map>
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
    case RunVerdict::Ok: return QStringLiteral("НОРМА");
    case RunVerdict::Fail: return QStringLiteral("НЕ НОРМА");
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

ReportPaths writeHtmlCsvReport(const ScenarioRunResult& run, const std::string& directoryPath,
                               const ProductionReportMetadata& production)
{
    QDir directory(QString::fromUtf8(directoryPath));
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        throw std::runtime_error("Cannot create report directory");
    }
    const QString runId = QString::fromUtf8(run.runId);
    const QString stem = QStringLiteral("Ведомость_каналов_%1").arg(runId);
    const QString htmlPath = directory.filePath(stem + QStringLiteral(".html"));
    const QString csvPath = directory.filePath(stem + QStringLiteral(".csv"));
    const QString tuPath = directory.filePath(
        QStringLiteral("Протокол_ТУ_%1.html").arg(runId));

    std::vector<std::pair<const StepRunResult*, const MeasurementResult*>> measurements;
    std::function<void(const StepRunResult&)> collect = [&](const StepRunResult& step) {
        for (const auto& value : step.measurements) measurements.push_back({&step, &value});
        for (const auto& child : step.children) collect(child);
    };
    for (const auto& step : run.steps) collect(step);
    bool hasYtp = false;
    bool hasYalk = false;
    int normalCount = 0;
    int abnormalCount = 0;
    for (const auto& [step, value] : measurements) {
        Q_UNUSED(step);
        hasYtp = hasYtp || value->attributes.count("ytp_channel") != 0;
        hasYalk = hasYalk || value->attributes.count("ulk_address") != 0;
        if (value->verdict == RunVerdict::Ok) ++normalCount;
        else if (value->verdict == RunVerdict::Fail) ++abnormalCount;
    }
    const bool ytpReport = hasYtp && !hasYalk;
    const bool mixedReport = hasYtp && hasYalk;
    const bool formalTu = run.scenarioId == "ubsi.468157.002.yalk.tu5_6"
        || run.scenarioId == "ubsi.468157.002.yalk.contact_thresholds"
        || run.scenarioId == "ubsi.468157.002.ytp.tu5_6"
        || run.scenarioId == "ubsi.468157.002.ulk.combined.check";
    const bool combinedTu = run.scenarioId == "ubsi.468157.002.ulk.combined.check";

    QSaveFile csv(csvPath);
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Text)) throw std::runtime_error(csv.errorString().toUtf8().toStdString());
    csv.write("\xEF\xBB\xBF");
    QTextStream csvStream(&csv);
    csvStream.setEncoding(QStringConverter::Utf8);
    if (mixedReport) {
        csvStream << QStringLiteral("Ячейка;Этап;Пункт ТУ;Канал/адрес;Точка;Эталон;Raw;Измерено;Абсолютная погрешность;Приведённая погрешность, %;Сигнал;Итог;Сообщение\n");
    } else if (ytpReport) {
        csvStream << QStringLiteral("Этап;Пункт ТУ;Канал ЯТП;Параметр;Задано, Ом;Эталон, Ом;Raw;Калибровка ноль;Калибровка шкала;ЯТП, Ом;Абсолютная погрешность, Ом;Приведённая погрешность, %;Оператор;Время подтверждения;Итог;Сообщение\n");
    } else {
        csvStream << QStringLiteral("Этап;Пункт ТУ;Параметр;Адрес УЛК;Код ИСД;Raw;Код ЯЛК;Сигнал;В7, В;ЯЛК, В;Абсолютная погрешность, В;Приведённая погрешность, %;Относительная погрешность, %;Нижний допуск;Верхний допуск;Итог;Сообщение\n");
    }
    const auto field = [](QString value) { return QStringLiteral("\"") + value.replace('"', QStringLiteral("\"\"")) + QStringLiteral("\""); };
    for (const auto& [step, value] : measurements) {
        const auto attribute = [value](const char* key) {
            const auto found = value->attributes.find(key);
            return found == value->attributes.end() ? QString() : raw(found->second);
        };
        if (value->parameterKey.rfind("ubsi.supply", 0) == 0
            || value->parameterKey.rfind("ubsi.yalk.cleanup", 0) == 0
            || value->attributes.count("evidence_reference") != 0) continue;
        const bool ytpValue = value->attributes.count("ytp_channel") != 0;
        if (mixedReport) {
            csvStream << field(ytpValue ? QStringLiteral("ЯТП") : QStringLiteral("ЯЛК")) << ';'
                      << field(raw(step->title)) << ';' << field(raw(step->tuRequirement)) << ';'
                      << field(ytpValue ? attribute("ytp_channel") : attribute("ulk_address")) << ';'
                      << field(ytpValue ? attribute("actual_reference_ohm") : attribute("command_v")) << ';'
                      << field(ytpValue ? attribute("actual_reference_ohm") : attribute("v7_v")) << ';'
                      << field(attribute("raw")) << ';'
                      << field(ytpValue ? attribute("measured_resistance_ohm") : attribute("yalk_v")) << ';'
                      << field(ytpValue ? attribute("absolute_error_ohm") : attribute("absolute_error_v")) << ';'
                      << field(attribute("reduced_error_percent")) << ';' << field(attribute("signal")) << ';'
                      << field(localVerdict(value->verdict)) << ';' << field(raw(value->message)) << '\n';
        } else if (ytpReport) {
            csvStream << field(raw(step->title)) << ';' << field(raw(step->tuRequirement)) << ';'
                      << field(attribute("ytp_channel")) << ';' << field(raw(value->title)) << ';'
                      << field(attribute("target_resistance_ohm")) << ';'
                      << field(attribute("actual_reference_ohm")) << ';'
                      << field(attribute("raw")) << ';'
                      << field(attribute("calibration_zero_raw")) << ';'
                      << field(attribute("calibration_full_raw")) << ';'
                      << field(attribute("measured_resistance_ohm")) << ';'
                      << field(attribute("absolute_error_ohm")) << ';'
                      << field(attribute("reduced_error_percent")) << ';'
                      << field(attribute("operator")) << ';' << field(attribute("timestamp")) << ';'
                      << field(localVerdict(value->verdict)) << ';' << field(raw(value->message)) << '\n';
        } else {
            csvStream << field(raw(step->title)) << ';' << field(raw(step->tuRequirement)) << ';'
                      << field(raw(value->title)) << ';' << field(attribute("ulk_address")) << ';'
                      << field(attribute("isd_code")) << ';' << field(attribute("raw")) << ';'
                      << field(attribute("analog_code")) << ';' << field(attribute("signal")) << ';'
                      << field(attribute("v7_v")) << ';' << field(attribute("yalk_v")) << ';'
                      << field(attribute("absolute_error_v")) << ';'
                      << field(attribute("reduced_error_percent")) << ';'
                      << field(attribute("relative_error_percent")) << ';'
                      << QString::number(value->lowerLimit, 'g', 15) << ';'
                      << QString::number(value->upperLimit, 'g', 15) << ';'
                      << field(localVerdict(value->verdict)) << ';' << field(raw(value->message)) << '\n';
        }
    }
    csvStream.flush();
    commit(csv);
    if (run.scenarioId.find("yalk") != std::string::npos) {
        const QString channelsPath = directory.filePath(QStringLiteral("channels.csv"));
        QFile::remove(channelsPath);
        if (!QFile::copy(csvPath, channelsPath)) throw std::runtime_error("Cannot create channels.csv");
    }

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
           << escape(run.profileVersion) << QStringLiteral("</b></div></div>")
           << QStringLiteral("<div class=\"summary\" style=\"margin-top:12px\"><div class=\"card\"><div>Вид результата</div><b>")
           << (formalTu ? QStringLiteral("Проверка по ТУ")
                        : QStringLiteral("Контрольный прогон — не полная проверка ТУ"))
           << QStringLiteral("</b></div><div class=\"card\"><div>Измерений в норме</div><b>")
           << normalCount << QStringLiteral("</b></div><div class=\"card\"><div>Измерений не в норме</div><b>")
           << abnormalCount << QStringLiteral("</b></div></div>");
    if (!production.componentSerial.empty()) {
        output << QStringLiteral("<h2>Production context</h2><table><tbody><tr><th>Изделие</th><td>")
               << escape(production.productSerial) << QStringLiteral("</td></tr><tr><th>Ячейка</th><td>")
               << escape(production.componentType) << QStringLiteral(" · SN ") << escape(production.componentSerial)
               << QStringLiteral("</td></tr><tr><th>Этап</th><td>") << escape(production.stage)
               << QStringLiteral("</td></tr></tbody></table>");
    }

    std::map<QString, std::vector<const MeasurementResult*>> chartGroups;
    for (const auto& [step, value] : measurements) {
        Q_UNUSED(step);
        const auto get = [value](const char* key) {
            const auto found = value->attributes.find(key);
            return found == value->attributes.end() ? QString() : raw(found->second);
        };
        const bool ytpValue = !get("ytp_channel").isEmpty();
        const QString point = ytpValue ? get("actual_reference_ohm") + QStringLiteral(" Ом")
                                       : get("command_v") + QStringLiteral(" В");
        if (!point.startsWith(' '))
            chartGroups[(ytpValue ? QStringLiteral("ЯТП · ") : QStringLiteral("ЯЛК · ")) + point].push_back(value);
    }
    if (!chartGroups.empty()) output << QStringLiteral("<h2>Отклонение по каналам</h2>");
    for (const auto& [title, values] : chartGroups) {
        double maximum = 0.5;
        for (const auto* value : values) {
            const auto found = value->attributes.find("reduced_error_percent");
            if (found != value->attributes.end()) maximum = std::max(maximum, std::abs(QString::fromStdString(found->second).toDouble()));
        }
        maximum *= 1.15;
        output << QStringLiteral("<div class=\"chart\"><b>") << title.toHtmlEscaped()
               << QStringLiteral("</b><svg viewBox=\"0 0 920 230\" role=\"img\" aria-label=\"Приведённая погрешность по каналам\">");
        const double toleranceY = 200.0 - 0.5 / maximum * 170.0;
        output << QStringLiteral("<line x1=\"30\" y1=\"200\" x2=\"900\" y2=\"200\" stroke=\"#8b949e\"/><line x1=\"30\" y1=\"")
               << toleranceY << QStringLiteral("\" x2=\"900\" y2=\"") << toleranceY
               << QStringLiteral("\" stroke=\"#b3261e\" stroke-dasharray=\"5 4\"/><text x=\"34\" y=\"")
               << (toleranceY - 4.0) << QStringLiteral("\" fill=\"#b3261e\">допуск 0,5 % шкалы</text>");
        const double step = 860.0 / std::max<std::size_t>(1, values.size());
        for (std::size_t index = 0; index < values.size(); ++index) {
            const auto* value = values[index];
            const auto found = value->attributes.find("reduced_error_percent");
            const double error = found == value->attributes.end() ? 0.0
                : std::abs(QString::fromStdString(found->second).toDouble());
            const double height = error / maximum * 170.0;
            const double x = 34.0 + index * step;
            output << QStringLiteral("<rect x=\"") << x << QStringLiteral("\" y=\"") << (200.0 - height)
                   << QStringLiteral("\" width=\"") << std::max(2.0, step - 2.0) << QStringLiteral("\" height=\"")
                   << std::max(1.0, height) << QStringLiteral("\" fill=\"")
                   << (value->verdict == RunVerdict::Ok ? QStringLiteral("#238636") : QStringLiteral("#b3261e"))
                   << QStringLiteral("\"/>");
        }
        output << QStringLiteral("<text x=\"35\" y=\"222\" fill=\"#586069\">каналы слева направо · зелёный — норма · красный — не норма</text></svg></div>");
    }

    output << (mixedReport
        ? QStringLiteral("<table><thead><tr><th>Ячейка</th><th>Канал/адрес</th><th>Точка</th><th>Эталон</th><th>Raw</th><th>Измерено</th><th>Ошибка</th><th>γ, %</th><th>Сигнал</th><th>Итог</th></tr></thead><tbody>")
        : ytpReport
        ? QStringLiteral("<table><thead><tr><th>Канал</th><th>Точка</th><th>Эталон, Ом</th><th>Raw</th><th>Калибровка 0 / шкала</th><th>ЯТП, Ом</th><th>Ошибка, Ом</th><th>γ, %</th><th>Итог</th></tr></thead><tbody>")
        : QStringLiteral("<table><thead><tr><th>Адрес</th><th>Точка</th><th>Raw / код</th><th>Сигнал</th><th>В7, В</th><th>ЯЛК, В</th><th>γ, %</th><th>Допуск</th><th>Итог</th></tr></thead><tbody>"));
    for (const auto& [step, value] : measurements) {
        Q_UNUSED(step);
        const auto attribute = [value](const char* key) {
            const auto found = value->attributes.find(key);
            return found == value->attributes.end() ? QString() : escape(found->second);
        };
        if (value->parameterKey.rfind("ubsi.supply", 0) == 0
            || value->parameterKey.rfind("ubsi.yalk.cleanup", 0) == 0
            || value->attributes.count("evidence_reference") != 0) continue;
        output << QStringLiteral("<tr class=\"") << QString::fromLatin1(toString(value->verdict))
               << QStringLiteral("\"><td>");
        const bool ytpValue = value->attributes.count("ytp_channel") != 0;
        if (mixedReport) {
            output << (ytpValue ? QStringLiteral("ЯТП") : QStringLiteral("ЯЛК"))
                   << QStringLiteral("</td><td>") << (ytpValue ? attribute("ytp_channel") : attribute("ulk_address"))
                   << QStringLiteral("</td><td>") << (ytpValue ? attribute("actual_reference_ohm") + QStringLiteral(" Ом") : attribute("command_v") + QStringLiteral(" В"))
                   << QStringLiteral("</td><td>") << (ytpValue ? attribute("actual_reference_ohm") : attribute("v7_v"))
                   << QStringLiteral("</td><td>") << attribute("raw")
                   << QStringLiteral("</td><td>") << (ytpValue ? attribute("measured_resistance_ohm") : attribute("yalk_v"))
                   << QStringLiteral("</td><td>") << (ytpValue ? attribute("absolute_error_ohm") : attribute("absolute_error_v"))
                   << QStringLiteral("</td><td>") << attribute("reduced_error_percent")
                   << QStringLiteral("</td><td>") << attribute("signal")
                   << QStringLiteral("</td><td>") << localVerdict(value->verdict)
                   << QStringLiteral("</td></tr>");
        } else if (ytpReport) {
            output << attribute("ytp_channel") << QStringLiteral("</td><td>") << attribute("actual_reference_ohm")
                   << QStringLiteral("</td><td>") << attribute("actual_reference_ohm")
                   << QStringLiteral("</td><td>") << attribute("raw")
                   << QStringLiteral("</td><td>") << attribute("calibration_zero_raw")
                   << QStringLiteral(" / ") << attribute("calibration_full_raw")
                   << QStringLiteral("</td><td>") << attribute("measured_resistance_ohm")
                   << QStringLiteral("</td><td>") << attribute("absolute_error_ohm")
                   << QStringLiteral("</td><td>") << attribute("reduced_error_percent")
                   << QStringLiteral("</td><td>") << localVerdict(value->verdict)
                   << QStringLiteral("</td></tr>");
        } else {
            output << attribute("ulk_address") << QStringLiteral("</td><td>") << escape(value->title)
                   << QStringLiteral("</td><td>") << attribute("raw") << QStringLiteral(" / ") << attribute("analog_code")
                   << QStringLiteral("</td><td>") << attribute("signal")
                   << QStringLiteral("</td><td>") << attribute("v7_v")
                   << QStringLiteral("</td><td>") << attribute("yalk_v")
                   << QStringLiteral("</td><td>") << attribute("reduced_error_percent")
                   << QStringLiteral("</td><td class=\"num\">")
                   << QString::number(value->lowerLimit, 'g', 10) << QStringLiteral(" … ")
                   << QString::number(value->upperLimit, 'g', 10) << QStringLiteral("</td><td>")
                   << localVerdict(value->verdict) << QStringLiteral("</td></tr>");
        }
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

    // The operator-facing TU protocol is deliberately independent from the
    // technical channel sheet above.  It is issued only for a normative
    // outcome and contains exactly the four fields accepted for this delivery.
    if (run.verdict != RunVerdict::Ok && run.verdict != RunVerdict::Fail) {
        return {tuPath.toUtf8().toStdString(), csvPath.toUtf8().toStdString(),
                std::string(), htmlPath.toUtf8().toStdString()};
    }
    QString operatorName = QStringLiteral("не указан");
    for (const auto& [step, value] : measurements) {
        Q_UNUSED(step);
        const auto found = value->attributes.find("operator");
        if (found != value->attributes.end() && !found->second.empty()) {
            operatorName = escape(found->second);
            break;
        }
    }
    const bool yvpExcluded = std::any_of(run.events.begin(), run.events.end(),
        [](const RunEvent& event) {
            const auto found = event.data.find("yvp_included");
            return found != event.data.end() && found->second == "false";
        });
    const QString resultText = localVerdict(run.verdict)
        + (yvpExcluded ? QStringLiteral(" · ЯВП не выполнялась") : QString());
    QSaveFile tu(tuPath);
    if (!tu.open(QIODevice::WriteOnly | QIODevice::Text)) {
        throw std::runtime_error(tu.errorString().toUtf8().toStdString());
    }
    QTextStream brief(&tu);
    brief.setEncoding(QStringConverter::Utf8);
    brief << QStringLiteral("<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\"><title>Протокол ТУ</title><style>")
          << QStringLiteral("body{font:14px sans-serif;margin:24px;color:#000}table{border-collapse:collapse;width:100%;max-width:680px}th,td{border:1px solid #000;padding:10px;text-align:left}th{width:32%}@media print{body{margin:10mm}}</style></head><body><h1>Протокол ТУ</h1><table>")
          << QStringLiteral("<tr><th>Дата</th><td>") << iso(run.finishedAt) << QStringLiteral("</td></tr>")
          << QStringLiteral("<tr><th>Блок</th><td>") << (run.objectSerial.empty() ? QStringLiteral("не указан") : escape(run.objectSerial)) << QStringLiteral("</td></tr>")
          << QStringLiteral("<tr><th>Оператор</th><td>") << operatorName << QStringLiteral("</td></tr>")
          << QStringLiteral("<tr><th>Результат</th><td><b>") << resultText << QStringLiteral("</b></td></tr></table></body></html>");
    brief.flush();
    commit(tu);
    return {tuPath.toUtf8().toStdString(), csvPath.toUtf8().toStdString(),
            tuPath.toUtf8().toStdString(), htmlPath.toUtf8().toStdString()};
}

} // namespace orbita::stand
