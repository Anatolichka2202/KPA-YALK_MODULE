#include "tu_report_writer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextStream>

#include <algorithm>
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
    case tu::RunVerdict::Fail:
    case tu::RunVerdict::Error: return QStringLiteral("НЕ НОРМА");
    case tu::RunVerdict::Incomplete: return QStringLiteral("НЕ ЗАВЕРШЕНО");
    case tu::RunVerdict::Aborted: return QStringLiteral("ОСТАНОВЛЕНО");
    case tu::RunVerdict::NotRun: return QStringLiteral("НЕ ВЫПОЛНЕНО");
    }
    return QStringLiteral("НЕ ВЫПОЛНЕНО");
}

QString reportDate(std::chrono::system_clock::time_point value)
{
    if (value.time_since_epoch().count() == 0)
        return QDateTime::currentDateTime().toString(QStringLiteral("dd.MM.yyyy"));
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        value.time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(milliseconds).toString(QStringLiteral("dd.MM.yyyy"));
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

QString attribute(const tu::MeasurementResult& value, const char* key)
{
    const auto found = value.attributes.find(key);
    return found == value.attributes.end() ? QString{} : text(found->second);
}

bool enabledAttribute(const tu::MeasurementResult& value, const char* key)
{
    const QString current = attribute(value, key).trimmed().toLower();
    return current == QStringLiteral("true") || current == QStringLiteral("1")
        || current == QStringLiteral("yes");
}

bool rawEngineeringMeasurement(const tu::MeasurementResult& value)
{
    return value.parameterKey.rfind("ubsi.yvp.raw.", 0) == 0;
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

const tu::StepRunResult* findStep(const std::vector<tu::StepRunResult>& steps,
                                  const std::string& nodeId)
{
    for (const auto& step : steps) {
        if (step.nodeId == nodeId) return &step;
        if (const auto* child = findStep(step.children, nodeId)) return child;
    }
    return nullptr;
}

void visitMeasurements(const std::vector<tu::StepRunResult>& steps,
                       const std::function<void(const tu::StepRunResult&, const tu::MeasurementResult&)>& visitor)
{
    for (const auto& step : steps) {
        for (const auto& measurement : step.measurements) visitor(step, measurement);
        visitMeasurements(step.children, visitor);
    }
}

void section(QTextStream& out, const QString& title, const QString& tu = {})
{
    out << QLatin1Char('\n') << title << QLatin1Char('\n');
    if (!tu.isEmpty()) out << QStringLiteral("(пункт %1 ТУ)\n").arg(tu);
    out << QLatin1Char('\n');
}

QString number(double value, int precision = 4)
{
    if (!std::isfinite(value)) return QStringLiteral("—");
    QString result = QString::number(value, 'f', precision);
    while (result.contains(QLatin1Char('.')) && result.endsWith(QLatin1Char('0'))) result.chop(1);
    if (result.endsWith(QLatin1Char('.'))) result.chop(1);
    return result;
}

void writeReadiness(QTextStream& out, const tu::ScenarioRunResult& run)
{
    const auto* step = findStep(run.steps, "readiness");
    if (!step) return;
    section(out, QStringLiteral("ПРОВЕРКА ГОТОВНОСТИ УБСИ ПОСЛЕ ПОДАЧИ НАПРЯЖЕНИЯ"),
            QStringLiteral("1.1.4.13"));
    out << QStringLiteral("Признак готовности: получение корректной телеметрической информации УБСИ.\n");
    if (!step->measurements.empty()) {
        const auto& value = step->measurements.front();
        out << QStringLiteral("Признак готовности появился через %1 с. %2\n")
            .arg(number(reportMeasured(value), 3), protocolVerdict(value.verdict));
    }
    out << QStringLiteral("Допустимое время готовности: не более 30 с.\n")
        << QStringLiteral("ПРОВЕРКА ГОТОВНОСТИ УБСИ    %1\n").arg(protocolVerdict(step->verdict));
}

void writePower(QTextStream& out, const tu::ScenarioRunResult& run)
{
    const auto* step = findStep(run.steps, "supply_range");
    if (!step) return;

    section(out, QStringLiteral("ПРОВЕРКА РАБОТОСПОСОБНОСТИ УБСИ ПРИ ИЗМЕНЕНИИ ПИТАЮЩЕГО НАПРЯЖЕНИЯ"),
            QStringLiteral("1.1.4.3"));
    for (const auto& value : step->measurements) {
        const QString key = text(value.parameterKey);
        const QString setpoint = attribute(value, "setpoint_v");
        if (key == QStringLiteral("ubsi.supply.voltage")) {
            out << QStringLiteral("АКИП %1 В    Фактическое напряжение: %2 В    %3\n")
                .arg(setpoint, number(reportMeasured(value), 3), protocolVerdict(value.verdict));
        } else if (key == QStringLiteral("ubsi.supply.data_alive")) {
            out << QStringLiteral("АКИП %1 В    Телеметрическая информация принимается    %2\n")
                .arg(setpoint, protocolVerdict(value.verdict));
        } else if (key == QStringLiteral("ubsi.supply.survival_voltage")) {
            out << QStringLiteral("АКИП %1 В    Фактическое напряжение: %2 В    Выдержка: %3 с    %4\n")
                .arg(number(value.reference, 1), number(reportMeasured(value), 3),
                     attribute(value, "duration_s"), protocolVerdict(value.verdict));
        } else if (key == QStringLiteral("ubsi.supply.survived")) {
            out << QStringLiteral("Восстановление на 27 В после выдержки    Телеметрическая информация принимается    %1\n")
                .arg(protocolVerdict(value.verdict));
        }
    }
    out << QStringLiteral("ПРОВЕРКА РАБОТОСПОСОБНОСТИ УБСИ ПРИ ИЗМЕНЕНИИ ПИТАЮЩЕГО НАПРЯЖЕНИЯ    %1\n")
        .arg(protocolVerdict(step->verdict));

    section(out, QStringLiteral("ПРОВЕРКА ТОКА ПОТРЕБЛЕНИЯ УБСИ"), QStringLiteral("1.1.4.5"));
    for (const auto& value : step->measurements) {
        if (value.parameterKey != "ubsi.supply.total_current") continue;
        out << QStringLiteral("АКИП %1 В    Ток потребления: %2 А    %3\n")
            .arg(attribute(value, "setpoint_v"), number(reportMeasured(value), 3),
                 protocolVerdict(value.verdict));
    }
    out << QStringLiteral("ТОК ПОТРЕБЛЕНИЯ ПРИ 24...35 В < 0.400 А    %1\n")
        .arg(protocolVerdict(step->verdict));
}

void writeYalkAnalog(QTextStream& out, const tu::ScenarioRunResult& run)
{
    const auto* step = findStep(run.steps, "yalk_channels");
    if (!step) return;
    section(out, QStringLiteral("ПРОВЕРКА ОПРОСА И ПРЕОБРАЗОВАНИЯ ПОТЕНЦИАЛЬНЫХ СИГНАЛОВ"),
            QStringLiteral("1.1.4.1, 1.1.4.14"));
    out << QStringLiteral("Оценка погрешности каналов ЯЛК-96.\n\n");
    for (const auto& value : step->measurements) {
        if (value.parameterKey.rfind("ubsi.yalk.channel.", 0) != 0) continue;
        const QString err = reportError(value);
        out << QStringLiteral("Канал %1   ЯЛК-96: %2 В   Вольтметр: %3 В")
            .arg(attribute(value, "ulk_address"), number(reportMeasured(value), 4),
                 attribute(value, "v7_v"));
        if (!err.isEmpty()) out << QStringLiteral("   Погрешность: ") << err;
        out << QStringLiteral("   %1\n").arg(protocolVerdict(value.verdict));
    }
    out << QStringLiteral("ПРОВЕРКА ПОТЕНЦИАЛЬНЫХ КАНАЛОВ ЯЛК-96    %1\n")
        .arg(protocolVerdict(step->verdict));
}

void writeReference(QTextStream& out, const tu::ScenarioRunResult& run)
{
    const auto* step = findStep(run.steps, "yalk_reference_voltage");
    if (!step) return;
    section(out, QStringLiteral("ПРОВЕРКА ВЫСОКОСТАБИЛЬНОГО ЭТАЛОННОГО НАПРЯЖЕНИЯ"),
            QStringLiteral("1.1.4.9"));
    for (const auto& value : step->measurements) {
        out << QStringLiteral("Эталонное напряжение: %1 В   Допуск: 6.170...6.230 В   %2\n")
            .arg(number(reportMeasured(value), 4), protocolVerdict(value.verdict));
    }
    out << QStringLiteral("ПРОВЕРКА ВЫСОКОСТАБИЛЬНОГО ЭТАЛОННОГО НАПРЯЖЕНИЯ    %1\n")
        .arg(protocolVerdict(step->verdict));
}

void writeOpenCircuit(QTextStream& out, const tu::ScenarioRunResult& run)
{
    const auto* step = findStep(run.steps, "yalk_initial");
    if (!step) return;
    section(out, QStringLiteral("ИДЕНТИФИКАЦИЯ ОБРЫВА АНАЛОГОВОГО ВХОДА НАПРЯЖЕНИЕМ НИЖЕ 0 В"),
            QStringLiteral("1.1.4.10"));
    for (const auto& value : step->measurements) {
        if (value.parameterKey.rfind("ubsi.yalk.initial.", 0) != 0
            && value.parameterKey.rfind("ubsi.yalk.open_circuit.", 0) != 0) continue;
        out << QStringLiteral("Канал %1   ЯЛК-96: %2 В   %3\n")
            .arg(attribute(value, "ulk_address"), number(reportMeasured(value), 4),
                 protocolVerdict(value.verdict));
    }
    out << QStringLiteral("ИДЕНТИФИКАЦИЯ ОБРЫВА АНАЛОГОВЫХ ВХОДОВ    %1\n")
        .arg(protocolVerdict(step->verdict));
}

void writeOverload(QTextStream& out, const tu::ScenarioRunResult& run)
{
    const auto* step = findStep(run.steps, "yalk_overload");
    if (!step) return;
    section(out, QStringLiteral("ПРОВЕРКА ЗАЩИТЫ ОТ ПЕРЕГРУЗОК ±12 В"),
            QStringLiteral("1.1.4.11"));
    QString previousTarget;
    QString previousPolarity;
    for (const auto& value : step->measurements) {
        if (value.parameterKey.rfind("ubsi.yalk.overload.", 0) != 0) continue;
        const QString target = attribute(value, "stressed_channel");
        const QString polarity = attribute(value, "polarity");
        if (target != previousTarget || polarity != previousPolarity) {
            out << QLatin1Char('\n')
                << QStringLiteral("Перегрузка канала %1, воздействие %2\n").arg(target, polarity);
            previousTarget = target;
            previousPolarity = polarity;
        }
        out << QStringLiteral("  Канал %1   Код до: %2   Код после: %3   Δкод: %4   %5\n")
            .arg(attribute(value, "observed_channel"), attribute(value, "baseline_code"),
                 attribute(value, "current_code"), attribute(value, "delta_code"),
                 protocolVerdict(value.verdict));
    }
    out << QStringLiteral("\nПРОВЕРКА ЗАЩИТЫ ОТ ПЕРЕГРУЗОК ±12 В    %1\n")
        .arg(protocolVerdict(step->verdict));
}

void writeContacts(QTextStream& out, const tu::ScenarioRunResult& run)
{
    const auto* step = findStep(run.steps, "yalk_channels");
    if (!step) return;
    section(out, QStringLiteral("ПРОВЕРКА ОПРОСА И ПРЕОБРАЗОВАНИЯ КОНТАКТНЫХ СИГНАЛОВ"),
            QStringLiteral("1.1.4.1"));
    out << QStringLiteral("Проверка контактных каналов ЯЛК-96.\n\n");
    QString previousPoint;
    for (const auto& value : step->measurements) {
        if (value.parameterKey.rfind("ubsi.yalk.signal.", 0) != 0) continue;
        const QString point = attribute(value, "command_v");
        const QString expected = attribute(value, "expected_signal");
        if (point != previousPoint) {
            out << QStringLiteral("Состояние \"%1\" при %2 В\n").arg(expected, point);
            previousPoint = point;
        }
        out << QStringLiteral("Канал %1   Вольтметр: %2 В   Состояние: %3   Срабатывание: %4\n")
            .arg(attribute(value, "ulk_address"), attribute(value, "v7_v"),
                 number(reportMeasured(value), 0), protocolVerdict(value.verdict));
    }
    out << QStringLiteral("\nПРОВЕРКА КОНТАКТНЫХ КАНАЛОВ ЯЛК-96    %1\n")
        .arg(protocolVerdict(step->verdict));
}

void writeYtp(QTextStream& out, const tu::ScenarioRunResult& run)
{
    const auto* step = findStep(run.steps, "ytp_channels");
    if (!step) return;
    section(out, QStringLiteral("ПРОВЕРКА ОПРОСА И ПРЕОБРАЗОВАНИЯ ИНФОРМАЦИИ ОТ ТЕРМОМЕТРОВ СОПРОТИВЛЕНИЯ"),
            QStringLiteral("1.1.4.1, 1.1.4.14"));
    QString previousPoint;
    for (const auto& value : step->measurements) {
        const QString target = attribute(value, "target_resistance_ohm");
        if (!target.isEmpty() && target != previousPoint) {
            out << QLatin1Char('\n') << QStringLiteral("Установлено сопротивление Р4831: %1 Ом\n").arg(target);
            previousPoint = target;
        }
        const QString channel = !attribute(value, "ytp_channel").isEmpty()
            ? attribute(value, "ytp_channel") : attribute(value, "channel");
        out << QStringLiteral("Канал %1   ЯТП: %2 %3")
            .arg(channel, number(reportMeasured(value), 4), text(value.unit));
        const QString err = reportError(value);
        if (!err.isEmpty()) out << QStringLiteral("   Погрешность: ") << err;
        out << QStringLiteral("   %1\n").arg(protocolVerdict(value.verdict));
    }
    out << QStringLiteral("\nПРОВЕРКА ТЕМПЕРАТУРНЫХ КАНАЛОВ ЯТП    %1\n")
        .arg(protocolVerdict(step->verdict));
}

void writeYvp(QTextStream& out, const tu::ScenarioRunResult& run)
{
    const auto* step = findStep(run.steps, "yvp_channels");
    if (!step) return;
    section(out, QStringLiteral("ПРОВЕРКА ОПРОСА И ПРЕОБРАЗОВАНИЯ ИНФОРМАЦИИ ОТ ВИБРОДАТЧИКОВ"),
            QStringLiteral("1.1.4.1, 1.1.4.7, 1.1.4.8, 1.1.4.14"));

    out << QStringLiteral("Проверка АЧХ\n\n");
    QString previousChannel;
    for (const auto& value : step->measurements) {
        if (rawEngineeringMeasurement(value)) continue;
        const QString criterion = attribute(value, "criterion");
        const QString frequency = attribute(value, "frequency_hz");
        if (criterion != QStringLiteral("afc") && frequency.isEmpty()) continue;
        if (criterion == QStringLiteral("gain")) continue;
        QString channel = attribute(value, "yvp_channel");
        if (channel.isEmpty()) channel = attribute(value, "channel");
        if (channel != previousChannel) {
            out << QStringLiteral("Проверка канала №%1\n").arg(channel);
            previousChannel = channel;
        }
        out << QStringLiteral("Частота = %1 Гц   Значение = %2 %3")
            .arg(frequency, number(reportMeasured(value), 4), text(value.unit));
        const QString err = reportError(value);
        if (!err.isEmpty()) out << QStringLiteral("   Отклонение = ") << err;
        const QString attenuation = attribute(value, "attenuation_db");
        if (!attenuation.isEmpty()) out << QStringLiteral("   Затухание = %1 дБ").arg(attenuation);
        out << QStringLiteral("   %1\n").arg(protocolVerdict(value.verdict));
    }
    out << QStringLiteral("Проверка АЧХ каналов ячейки ЯВП    %1\n\n")
        .arg(protocolVerdict(step->verdict));

    out << QStringLiteral("Проверка коэффициентов усиления\n\n");
    previousChannel.clear();
    for (const auto& value : step->measurements) {
        if (rawEngineeringMeasurement(value)) continue;
        if (attribute(value, "criterion") != QStringLiteral("gain")
            && value.parameterKey.rfind("ubsi.yvp.gain.", 0) != 0) continue;
        QString channel = attribute(value, "yvp_channel");
        if (channel.isEmpty()) channel = attribute(value, "channel");
        if (channel != previousChannel) {
            out << QStringLiteral("Проверка канала №%1\n").arg(channel);
            previousChannel = channel;
        }
        out << QStringLiteral("%1 = %2 %3")
            .arg(text(value.title), number(reportMeasured(value), 4), text(value.unit));
        const QString err = reportError(value);
        if (!err.isEmpty()) out << QStringLiteral("   Отклонение = ") << err;
        out << QStringLiteral("   %1\n").arg(protocolVerdict(value.verdict));
    }
    out << QStringLiteral("ПРОВЕРКА ОПРОСА И ПРЕОБРАЗОВАНИЯ ИНФОРМАЦИИ ОТ ВИБРОДАТЧИКОВ    %1\n")
        .arg(protocolVerdict(step->verdict));
}

void writeAccuracySummary(QTextStream& out, const tu::ScenarioRunResult& run)
{
    double analogMax = 0.0;
    double temperatureMax = 0.0;
    double fastMax = 0.0;
    bool analogSeen = false;
    bool temperatureSeen = false;
    bool fastSeen = false;
    visitMeasurements(run.steps, [&](const auto&, const auto& value) {
        if (rawEngineeringMeasurement(value)) return;
        const QString reduced = attribute(value, "reduced_error_percent");
        const QString reportDeviation = attribute(value, "report_deviation_percent");
        bool ok = false;
        if (value.parameterKey.rfind("ubsi.yalk.channel.", 0) == 0 && !reduced.isEmpty()) {
            const double current = std::abs(reduced.toDouble(&ok));
            if (ok) { analogMax = std::max(analogMax, current); analogSeen = true; }
        } else if (value.parameterKey.rfind("ubsi.ytp.", 0) == 0 && !reduced.isEmpty()) {
            const double current = std::abs(reduced.toDouble(&ok));
            if (ok) { temperatureMax = std::max(temperatureMax, current); temperatureSeen = true; }
        } else if (value.parameterKey.rfind("ubsi.yvp.", 0) == 0 && !reportDeviation.isEmpty()) {
            const double current = std::abs(reportDeviation.toDouble(&ok));
            if (ok) { fastMax = std::max(fastMax, current); fastSeen = true; }
        }
    });

    section(out, QStringLiteral("ПРОВЕРКА ПОГРЕШНОСТЕЙ ОТ ШКАЛЫ ИЗМЕРЕНИЙ"),
            QStringLiteral("1.1.4.14"));
    if (analogSeen)
        out << QStringLiteral("Аналоговые параметры    максимальная погрешность %1 %    НОРМА\n")
            .arg(number(analogMax, 3));
    if (temperatureSeen)
        out << QStringLiteral("Температурные параметры    максимальная погрешность %1 %    НОРМА\n")
            .arg(number(temperatureMax, 3));
    if (fastSeen)
        out << QStringLiteral("Быстрые параметры    максимальная погрешность %1 %    НОРМА\n")
            .arg(number(fastMax, 3));
}

} // namespace

QString writeTuReport(const tu::ScenarioRunResult& result)
{
    const QString directory = reportDirectory();
    const QString base = QStringLiteral("TU_%1").arg(safeFilePart(text(result.runId)));
    const QString path = QDir(directory).filePath(base + QStringLiteral(".txt"));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error(QStringLiteral("Не удалось открыть отчёт: %1").arg(path).toUtf8().toStdString());

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QStringLiteral("Дата: ") << reportDate(result.finishedAt) << QLatin1Char('\n')
        << QStringLiteral("Оператор: __OPERATOR__\n\n")
        << QStringLiteral("РЕЗУЛЬТАТЫ ПРОВЕРКИ УБСИ В НОРМАЛЬНЫХ УСЛОВИЯХ\n");

    writeReadiness(out, result);
    writePower(out, result);
    writeYalkAnalog(out, result);
    writeReference(out, result);
    writeOpenCircuit(out, result);
    writeOverload(out, result);
    writeAccuracySummary(out, result);
    writeContacts(out, result);
    writeYtp(out, result);
    writeYvp(out, result);

    out << QLatin1Char('\n')
        << QStringLiteral("РЕЗУЛЬТАТЫ ПРОВЕРКИ УБСИ В НОРМАЛЬНЫХ УСЛОВИЯХ    ")
        << protocolVerdict(result.verdict) << QLatin1Char('\n');

    if (!file.commit())
        throw std::runtime_error(QStringLiteral("Не удалось сохранить отчёт: %1").arg(path).toUtf8().toStdString());
    return path;
}
