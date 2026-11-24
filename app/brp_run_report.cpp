#include "brp_run_report.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>

#include <stdexcept>

QString brpResultText(BrpResult result)
{
    switch (result) {
    case BrpResult::Pending: return QStringLiteral("Не выполнено");
    case BrpResult::Waiting: return QStringLiteral("Ожидает оператора");
    case BrpResult::Running: return QStringLiteral("Выполняется");
    case BrpResult::Passed: return QStringLiteral("Пройдено");
    case BrpResult::Failed: return QStringLiteral("Не пройдено");
    case BrpResult::Unavailable: return QStringLiteral("Не оценено");
    case BrpResult::Skipped: return QStringLiteral("Пропущено");
    case BrpResult::Interrupted: return QStringLiteral("Остановлено");
    }
    return {};
}

namespace {

QString csvRow(QStringList cells)
{
    for (auto& cell : cells) {
        cell.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        cell = QLatin1Char('"') + cell + QLatin1Char('"');
    }
    return cells.join(QLatin1Char(';')) + QLatin1Char('\n');
}

void save(const QString& path, const QString& content)
{
    QSaveFile file(path);
    const auto bytes = content.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error(QStringLiteral("Не удалось сохранить %1. %2")
            .arg(path, file.errorString()).toUtf8().toStdString());
}

} // namespace

QString writeBrpRunReport(const BrpRunReport& run)
{
    QString directory = qEnvironmentVariable("TU_REPORT_DIR");
    if (directory.isEmpty()) {
        const QString standRoot = QStringLiteral("C:/Orbita");
        const QString root = QFileInfo(standRoot).isDir() ? standRoot : QCoreApplication::applicationDirPath();
        directory = QDir(root).filePath(QStringLiteral("reports"));
    }
    if (!QDir().mkpath(directory))
        throw std::runtime_error(QStringLiteral("Не удалось создать каталог %1").arg(directory).toUtf8().toStdString());

    const QString mode = run.simulation ? QStringLiteral("ИМИТАЦИЯ") : QStringLiteral("Стенд");
    const QString started = run.started.toString(Qt::ISODate);
    const QString finished = run.finished.toString(Qt::ISODate);
    QString protocol;
    QTextStream text(&protocol);
    text << QStringLiteral("ПРОТОКОЛ КАДРОВЫХ ПРОВЕРОК БРП\n")
         << QStringLiteral("Режим: ") << mode << '\n'
         << QStringLiteral("Серийный номер: ") << run.serial << '\n'
         << QStringLiteral("Оператор: ") << run.operatorName << '\n'
         << QStringLiteral("Начало: ") << started << '\n'
         << QStringLiteral("Окончание: ") << finished << '\n'
         << QStringLiteral("Объём: интерфейс M16 и по одной точке на каждый указанный аналоговый вход.\n"
                           "Оценка относится к указанным измерениям по пунктам ТУ.\n\n");

    QString csv = csvRow({QStringLiteral("Режим"), QStringLiteral("Серийный номер"),
        QStringLiteral("Оператор"), QStringLiteral("Начало"), QStringLiteral("Окончание"),
        QStringLiteral("Строка таблицы 1"), QStringLiteral("Пункт ТУ"), QStringLiteral("Этап"),
        QStringLiteral("Адрес Орбита"), QStringLiteral("Результат"), QStringLiteral("Подробности"),
        QStringLiteral("Параметр"), QStringLiteral("Измерено"), QStringLiteral("Единица")});
    for (const auto& step : run.steps) {
        const auto row = step.row ? QString::number(step.row) : QString();
        const auto verdict = brpResultText(step.result);
        text << step.name;
        if (step.row) text << QStringLiteral(" · строка ") << row << QStringLiteral(" · ТУ ") << step.requirement;
        if (!step.address.isEmpty()) text << QStringLiteral(" · ") << step.address;
        text << QStringLiteral(" — ") << verdict << '\n';
        if (!step.detail.isEmpty()) text << step.detail << '\n';
        const QStringList fields{mode, run.serial, run.operatorName, started, finished, row,
            step.requirement, step.name, step.address, verdict, step.detail};
        if (step.measurements.isEmpty()) csv += csvRow(fields + QStringList{QString(), QString(), QString()});
        for (const auto& measurement : step.measurements) {
            text << measurement.name << QStringLiteral(": ") << measurement.value << ' ' << measurement.unit << '\n';
            csv += csvRow(fields + QStringList{measurement.name, measurement.value, measurement.unit});
        }
        text << '\n';
    }
    text.flush();

    const auto base = QDir(directory).absoluteFilePath(QStringLiteral("TU_BRP_%1%2")
        .arg(run.simulation ? QStringLiteral("SIM_") : QString(), run.id));
    const auto html = QStringLiteral("<!doctype html><html lang=\"ru\"><meta charset=\"utf-8\">"
        "<title>Протокол кадровых проверок БРП</title>"
        "<style>body{margin:24px;color:#162436}pre{white-space:pre-wrap;font:14px sans-serif;line-height:1.5}</style>"
        "<pre>%1</pre></html>").arg(protocol.toHtmlEscaped());
    save(base + QStringLiteral(".txt"), protocol);
    save(base + QStringLiteral(".csv"), csv);
    const auto path = base + QStringLiteral(".html");
    save(path, html);
    return path;
}
