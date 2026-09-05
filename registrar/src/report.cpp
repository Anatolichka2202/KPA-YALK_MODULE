#include "report.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>

#include <stdexcept>

namespace ktma::registrar {
namespace {

QString escape(const std::string& value)
{
    return QString::fromUtf8(value).toHtmlEscaped();
}

QString stageCell(const ProductReport& report, const std::string& componentId, Stage stage)
{
    for (auto it = report.stageAttempts.rbegin(); it != report.stageAttempts.rend(); ++it) {
        if (it->componentId == componentId && it->stage == stage) {
            QString value = QString::fromUtf8(toString(it->verdict));
            if (!it->runId.empty()) value += QStringLiteral(" · run_id=") + escape(it->runId);
            return value;
        }
    }
    return QStringLiteral("НЕПОЛНАЯ");
}

} // namespace

std::string writeProductReportHtml(
    const ProductReport& report,
    const std::string& outputPath)
{
    const QFileInfo info(QString::fromStdString(outputPath));
    if (!QDir().mkpath(info.absolutePath())) {
        throw std::runtime_error("cannot create product report directory");
    }

    const QString verdict = QString::fromUtf8(toString(report.verdict));
    QString html;
    html += QStringLiteral("<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\">");
    html += QStringLiteral("<title>Журнал КТМА — ") + escape(report.product.serialNumber);
    html += QStringLiteral("</title><style>body{font-family:Segoe UI,Arial;background:#101419;"
        "color:#e8edf2;margin:28px}table{border-collapse:collapse;width:100%;margin-top:16px}"
        "th,td{border:1px solid #38424e;padding:7px;text-align:left}th{background:#1c2530}"
        ".ok{color:#69d39a}.fail{color:#ff7d86}.incomplete{color:#f0bd67}</style></head><body>");
    html += QStringLiteral("<h1>Журнал КТМА</h1><p>Изделие: <b>")
        + escape(report.product.serialNumber) + QStringLiteral("</b> · тип: ")
        + escape(report.product.productType) + QStringLiteral(" · итог: <b class=\"")
        + (report.verdict == Verdict::Ok ? QStringLiteral("ok")
           : report.verdict == Verdict::Fail ? QStringLiteral("fail")
                                             : QStringLiteral("incomplete"))
        + QStringLiteral("\">") + verdict + QStringLiteral("</b></p>");
    html += QStringLiteral("<h2>Состав и этапы</h2><table><thead><tr><th>Тип</th><th>SN</th>"
                           "<th>Состояние</th><th>Входная</th><th>После вибрации</th>"
                           "<th>После климата</th><th>Финальная</th><th>Причина снятия</th>"
                           "</tr></thead><tbody>");
    for (const auto& component : report.components) {
        html += QStringLiteral("<tr><td>") + escape(component.componentType)
            + QStringLiteral("</td><td>") + escape(component.serialNumber)
            + QStringLiteral("</td><td>") + (component.active ? QStringLiteral("ACTIVE")
                                                                    : QStringLiteral("REMOVED"))
            + QStringLiteral("</td><td>") + stageCell(report, component.componentId, Stage::InitialElectrical)
            + QStringLiteral("</td><td>") + stageCell(report, component.componentId, Stage::PostVibrationElectrical)
            + QStringLiteral("</td><td>") + stageCell(report, component.componentId, Stage::PostClimateElectrical)
            + QStringLiteral("</td><td>") + stageCell(report, component.componentId, Stage::FinalElectrical)
            + QStringLiteral("</td><td>") + escape(component.removalReason)
            + QStringLiteral("</td></tr>");
    }
    html += QStringLiteral("</tbody></table><p>Подробные измерения и события: runs.db; "
                           "связь выполняется через stage_attempt_id → run_id.</p></body></html>");

    QSaveFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly) || file.write(html.toUtf8()) != html.toUtf8().size()
        || !file.commit()) {
        throw std::runtime_error("cannot write product report");
    }
    return info.absoluteFilePath().toStdString();
}

} // namespace ktma::registrar
