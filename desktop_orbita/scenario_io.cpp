#include "scenario_io.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>

std::optional<Scenario> loadScenario(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "loadScenario: не удалось открыть файл:" << path;
        return std::nullopt;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    Scenario scenario;
    bool hasName = false;

    while (!in.atEnd()) {
        QString rawLine = in.readLine();
        // Убираем ведущие/хвостовые пробелы
        QString line = rawLine.trimmed();

        // Пустые строки и комментарии пропускаем
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        // SCENARIO <название>
        if (line.startsWith("SCENARIO ")) {
            scenario.name = line.mid(9).trimmed();
            hasName = true;
            continue;
        }

        // CMD <текст>
        if (line.startsWith("CMD ")) {
            ScenarioStep step;
            step.kind = StepKind::Command;
            step.text = line.mid(4).trimmed();
            scenario.steps.append(step);
            continue;
        }

        // CHECK <адрес> <lo> <hi>
        if (line.startsWith("CHECK ")) {
            // Разбиваем по пробелам, пропуская пустые части
            QStringList parts = line.mid(6).trimmed().split(' ', Qt::SkipEmptyParts);
            if (parts.size() < 3) {
                qWarning() << "loadScenario: строка CHECK — недостаточно полей, пропуск:" << line;
                continue;
            }

            bool okLo = false, okHi = false;
            double lo = parts[1].toDouble(&okLo);
            double hi = parts[2].toDouble(&okHi);

            if (!okLo || !okHi) {
                qWarning() << "loadScenario: строка CHECK — неверные числа lo/hi, пропуск:" << line;
                continue;
            }

            ScenarioStep step;
            step.kind    = StepKind::Check;
            step.address = parts[0];
            step.lo      = lo;
            step.hi      = hi;
            // Необязательный текстовый описание (все части после hi)
            if (parts.size() > 3)
                step.text = parts.mid(3).join(' ');
            else
                step.text = step.address;
            scenario.steps.append(step);
            continue;
        }

        qWarning() << "loadScenario: нераспознанная строка, пропуск:" << line;
    }

    if (!hasName)
        scenario.name = QFileInfo(path).baseName();

    return scenario;
}

bool saveScenario(const Scenario& scenario, const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "saveScenario: не удалось открыть файл для записи:" << path;
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    out << "# Сценарий проверки — Орбита-IV\n";
    out << "SCENARIO " << scenario.name << "\n";
    out << "\n";

    for (const ScenarioStep& step : scenario.steps) {
        if (step.kind == StepKind::Command) {
            out << "CMD " << step.text << "\n";
        } else {
            // CHECK <адрес> <lo> <hi> [описание]
            out << "CHECK " << step.address
                << " " << QString::number(step.lo, 'g', 10)
                << " " << QString::number(step.hi, 'g', 10);
            // Если текст отличается от адреса — пишем его как доп. поле
            if (!step.text.isEmpty() && step.text != step.address)
                out << " " << step.text;
            out << "\n";
        }
    }

    return true;
}
