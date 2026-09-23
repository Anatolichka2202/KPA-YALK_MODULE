#include "tu_report_writer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
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
    case tu::RunVerdict::Fail: return QStringLiteral("НЕ НОРМА");
    case tu::RunVerdict::Error: return QStringLiteral("ОШИБКА СТЕНДА");
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

bool hasKey(const tu::MeasurementResult& value, const char* prefix)
{
    return value.parameterKey.rfind(prefix, 0) == 0;
}

QString number(double value, int precision = 3)
{
    if (!std::isfinite(value)) return QStringLiteral("—");
    return QLocale::c().toString(value, 'f', precision);
}

QString signedNumber(double value, int precision = 2)
{
    if (!std::isfinite(value)) return QStringLiteral("—");
    return (value >= 0.0 ? QStringLiteral("+") : QString{}) + number(value, precision);
}

QString reportValue(const tu::MeasurementResult& value, int precision = 3)
{
    return number(reportMeasured(value), precision);
}

double numberAttribute(const tu::MeasurementResult& value, const char* key)
{
    bool ok = false;
    const double parsed = attribute(value, key).toDouble(&ok);
    return ok ? parsed : std::numeric_limits<double>::quiet_NaN();
}

const tu::StepRunResult* findStep(const std::vector<tu::StepRunResult>& steps,
                                  const char* nodeId)
{
    for (const auto& step : steps) {
        if (step.nodeId == nodeId) return &step;
        if (const auto* child = findStep(step.children, nodeId)) return child;
    }
    return nullptr;
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

void writeReadiness(QString& output, const tu::StepRunResult& step)
{
    section(output, QStringLiteral("ПРОВЕРКА ГОТОВНОСТИ УБСИ ПОСЛЕ ПОДАЧИ НАПРЯЖЕНИЯ"),
            QStringLiteral("1.1.4.13"));
    const auto found = std::find_if(step.measurements.begin(), step.measurements.end(),
        [](const auto& value) { return hasKey(value, "ubsi.ready_time"); });
    output += QStringLiteral("Признак готовности: получение корректной телеметрической информации УБСИ.\n");
    if (found != step.measurements.end()) {
        output += QStringLiteral("Признак готовности появился через %1 с.\n")
            .arg(reportValue(*found, 2));
        output += QStringLiteral("Допустимое время готовности: не более %1 с.\n")
            .arg(number(found->upperLimit, 0));
    }
    sectionResult(output, QStringLiteral("ПРОВЕРКА ГОТОВНОСТИ УБСИ"), step.verdict);
}

void writePower(QString& output, const tu::StepRunResult& step)
{
    section(output, QStringLiteral("ПРОВЕРКА РАБОТОСПОСОБНОСТИ УБСИ ПРИ ИЗМЕНЕНИИ ПИТАЮЩЕГО НАПРЯЖЕНИЯ"),
            QStringLiteral("1.1.4.3"));
    for (const auto& value : step.measurements) {
        if (hasKey(value, "ubsi.supply.voltage")) {
            const QString setpoint = attribute(value, "setpoint_v");
            output += QStringLiteral("АКИП %1 В    Фактическое напряжение: %2 В    Телеметрическая информация %3    %4\n")
                .arg(setpoint, reportValue(value),
                     protocolVerdict(value.verdict) == QStringLiteral("НОРМА")
                         ? QStringLiteral("принимается") : QStringLiteral("не подтверждена"),
                     protocolVerdict(value.verdict));
        } else if (hasKey(value, "ubsi.supply.survival_voltage")) {
            const QString setpoint = number(value.reference, 0);
            const double duration = numberAttribute(value, "duration_s");
            const QString hold = std::isfinite(duration)
                ? duration >= 60.0 && std::fmod(duration, 60.0) == 0.0
                    ? QStringLiteral("%1 мин").arg(number(duration / 60.0, 0))
                    : QStringLiteral("%1 с").arg(number(duration, 0))
                : QStringLiteral("—");
            output += QStringLiteral("АКИП %1 В    Фактическое напряжение: %2 В    Выдержка: %3    %4\n")
                .arg(setpoint, reportValue(value), hold, protocolVerdict(value.verdict));
        } else if (hasKey(value, "ubsi.supply.survived")) {
            output += QStringLiteral("АКИП 27 В    Восстановление после выдержки, телеметрия принимается    %1\n")
                .arg(protocolVerdict(value.verdict));
        }
    }
    sectionResult(output, QStringLiteral("ПРОВЕРКА РАБОТОСПОСОБНОСТИ УБСИ ПРИ ИЗМЕНЕНИИ ПИТАЮЩЕГО НАПРЯЖЕНИЯ"),
                  step.verdict);

    section(output, QStringLiteral("ПРОВЕРКА ТОКА ПОТРЕБЛЕНИЯ УБСИ"), QStringLiteral("1.1.4.5"));
    double limit = std::numeric_limits<double>::quiet_NaN();
    for (const auto& value : step.measurements) {
        if (!hasKey(value, "ubsi.supply.total_current")) continue;
        limit = value.upperLimit;
        const double setpoint = numberAttribute(value, "setpoint_v");
        const bool normative = std::isfinite(setpoint) && setpoint >= 24.0 && setpoint <= 35.0;
        output += QStringLiteral("АКИП %1 В    Ток потребления: %2 А%3\n")
            .arg(number(setpoint, 0), reportValue(value),
                 normative ? QStringLiteral("    %1").arg(protocolVerdict(value.verdict)) : QString{});
    }
    if (std::isfinite(limit)) {
        output += QStringLiteral("\nТок потребления при напряжении питания 24...35 В ≤ %1 А    %2\n")
            .arg(number(limit, 3), protocolVerdict(step.verdict));
    }
    sectionResult(output, QStringLiteral("ПРОВЕРКА ТОКА ПОТРЕБЛЕНИЯ УБСИ"), step.verdict);
}

using MeasurementsByChannel = std::map<int, std::vector<const tu::MeasurementResult*>>;

MeasurementsByChannel byChannel(const tu::StepRunResult& step, const char* prefix,
                                const char* channelAttribute)
{
    MeasurementsByChannel grouped;
    for (const auto& value : step.measurements) {
        if (!hasKey(value, prefix)) continue;
        bool ok = false;
        const int channel = attribute(value, channelAttribute).toInt(&ok);
        if (ok) grouped[channel].push_back(&value);
    }
    return grouped;
}

void writeYalkAnalog(QString& output, const tu::StepRunResult& step)
{
    const auto analog = byChannel(step, "ubsi.yalk.channel.", "ulk_address");
    section(output, QStringLiteral("ПРОВЕРКА ОПРОСА И ПРЕОБРАЗОВАНИЯ АНАЛОГОВЫХ СИГНАЛОВ"),
            QStringLiteral("1.1.4.1, 1.1.4.14"));
    output += QStringLiteral("Оценка погрешности 80 рабочих каналов ЯЛК-96.\n"
                             "Контрольные точки: 0 В; 3.1 В; 6.2 В.\n"
                             "Допустимая приведенная погрешность: ±0.5 % от шкалы 0...6.2 В.\n");
    for (const auto& [channel, values] : analog) {
        output += QStringLiteral("\nПроверка канала №%1\n").arg(channel);
        tu::RunVerdict channelVerdict = tu::RunVerdict::Ok;
        for (const auto* value : values) {
            const QString error = reportError(*value);
            output += QStringLiteral("Канал %1    ЯЛК-96: %2 В    Вольтметр: %3 В    Погрешность: %4    %5\n")
                .arg(QString::number(channel).rightJustified(2, QLatin1Char(' ')), reportValue(*value),
                     number(numberAttribute(*value, "v7_v")),
                     error.isEmpty() ? QStringLiteral("—") : error,
                     protocolVerdict(value->verdict));
            channelVerdict = tu::combineVerdicts(channelVerdict, value->verdict);
        }
        output += QStringLiteral("Проверка канала №%1    %2\n")
            .arg(channel).arg(protocolVerdict(channelVerdict));
    }
    tu::RunVerdict analogVerdict = tu::RunVerdict::Ok;
    for (const auto& [channel, values] : analog)
        for (const auto* value : values)
            analogVerdict = tu::combineVerdicts(analogVerdict, value->verdict);
    sectionResult(output, QStringLiteral("ПРОВЕРКА ОПРОСА И ПРЕОБРАЗОВАНИЯ АНАЛОГОВЫХ СИГНАЛОВ"),
                  analog.empty() ? tu::RunVerdict::NotRun : analogVerdict);
}

void writeYalkContact(QString& output, const tu::StepRunResult& step)
{
    section(output, QStringLiteral("ПРОВЕРКА ОПРОСА И ПРЕОБРАЗОВАНИЯ КОНТАКТНЫХ СИГНАЛОВ"),
            QStringLiteral("1.1.4.1"));
    output += QStringLiteral("Проверка контактного признака 80 рабочих каналов ЯЛК-96.\n");
    std::map<double, std::vector<const tu::MeasurementResult*>> byPoint;
    for (const auto& value : step.measurements) {
        if (!hasKey(value, "ubsi.yalk.signal.")) continue;
        const double volts = numberAttribute(value, "command_v");
        if (std::isfinite(volts)) byPoint[volts].push_back(&value);
    }
    tu::RunVerdict contactVerdict = tu::RunVerdict::Ok;
    for (const auto& [volts, values] : byPoint) {
        if (values.empty()) continue;
        output += QStringLiteral("\nСостояние \"%1\" при входном напряжении %2 В\n")
            .arg(number(values.front()->reference, 0), number(volts, 1));
        for (const auto* value : values) {
            const QString channel = attribute(*value, "ulk_address").rightJustified(2, QLatin1Char(' '));
            output += QStringLiteral("Канал %1    Вольтметр: %2 В    Состояние: %3    Срабатывание: %4\n")
                .arg(channel, number(numberAttribute(*value, "v7_v")),
                     reportValue(*value, 0), protocolVerdict(value->verdict));
            contactVerdict = tu::combineVerdicts(contactVerdict, value->verdict);
        }
    }
    sectionResult(output, QStringLiteral("ПРОВЕРКА КОНТАКТНЫХ КАНАЛОВ ЯЛК-96"),
                  byPoint.empty() ? tu::RunVerdict::NotRun : contactVerdict);
}

void writeYtp(QString& output, const tu::StepRunResult& step)
{
    section(output, QStringLiteral("ПРОВЕРКА ОПРОСА И ПРЕОБРАЗОВАНИЯ ИНФОРМАЦИИ ОТ ТЕРМОМЕТРОВ СОПРОТИВЛЕНИЯ"),
            QStringLiteral("1.1.4.1, 1.1.4.14"));
    output += QStringLiteral("Проверка 30 каналов ЯТП.\nКонтрольные точки Р4831: 0 Ом; 120 Ом; 240 Ом.\n"
                             "Допустимая приведенная погрешность: ±0.5 % от шкалы 0...240 Ом.\n");
    std::map<double, std::vector<const tu::MeasurementResult*>> byPoint;
    for (const auto& value : step.measurements) {
        if (!hasKey(value, "ubsi.ytp.channel.")) continue;
        const double ohms = numberAttribute(value, "target_resistance_ohm");
        if (std::isfinite(ohms)) byPoint[ohms].push_back(&value);
    }
    tu::RunVerdict ytpVerdict = tu::RunVerdict::Ok;
    for (const auto& [ohms, values] : byPoint) {
        output += QStringLiteral("\nУстановлено сопротивление Р4831: %1 Ом\n")
            .arg(number(ohms, 2));
        for (const auto* value : values) {
            const QString error = reportError(*value);
            output += QStringLiteral("Канал %1    Р4831: %2 Ом    ЯТП: %3 Ом    Погрешность: %4    %5\n")
                .arg(attribute(*value, "ytp_channel").rightJustified(2, QLatin1Char(' ')),
                     number(numberAttribute(*value, "actual_reference_ohm"), 2), reportValue(*value, 2),
                     error.isEmpty() ? QStringLiteral("—") : error,
                     protocolVerdict(value->verdict));
            ytpVerdict = tu::combineVerdicts(ytpVerdict, value->verdict);
        }
    }
    sectionResult(output, QStringLiteral("ПРОВЕРКА ТЕМПЕРАТУРНЫХ КАНАЛОВ ЯТП"),
                  byPoint.empty() ? tu::RunVerdict::NotRun : ytpVerdict);
}

void writeYalkInitial(QString& output, const tu::StepRunResult& step)
{
    section(output, QStringLiteral("ИДЕНТИФИКАЦИЯ ОБРЫВА АНАЛОГОВОГО ВХОДА НАПРЯЖЕНИЕМ НИЖЕ 0 В"),
            QStringLiteral("1.1.4.10"));
    for (const auto& [channel, values] : byChannel(step, "ubsi.yalk.initial.", "ulk_address")) {
        const auto* value = values.front();
        output += QStringLiteral("Канал %1    ЯЛК-96: %2 В    %3\n")
            .arg(QString::number(channel).rightJustified(2, QLatin1Char(' ')),
                 reportValue(*value, 4), protocolVerdict(value->verdict));
    }
    sectionResult(output, QStringLiteral("ИДЕНТИФИКАЦИЯ ОБРЫВА АНАЛОГОВЫХ ВХОДОВ"), step.verdict);
}

void writeYalkOverload(QString& output, const tu::StepRunResult& step)
{
    section(output, QStringLiteral("ПРОВЕРКА ЗАЩИТЫ АНАЛОГОВЫХ ВХОДОВ ОТ ПЕРЕГРУЗОК ±12 В"),
            QStringLiteral("1.1.4.11"));
    output += QStringLiteral("Для каждого перегружаемого канала контролируются остальные 79 рабочих каналов.\n"
                             "Критерий: изменение кода остальных каналов не более 5 единиц.\n");
    using Impact = std::pair<QString, unsigned>;
    std::map<Impact, std::vector<const tu::MeasurementResult*>> impacts;
    for (const auto& value : step.measurements) {
        if (!hasKey(value, "ubsi.yalk.overload.")) continue;
        bool ok = false;
        const unsigned target = attribute(value, "stressed_channel").toUInt(&ok);
        if (ok) impacts[{attribute(value, "polarity"), target}].push_back(&value);
    }
    for (const auto& [impact, values] : impacts) {
        output += QStringLiteral("\nПерегрузка канала %1, воздействие %2\n")
            .arg(impact.second).arg(impact.first);
        for (const auto* value : values) {
            const double delta = numberAttribute(*value, "delta_code");
            output += QStringLiteral("  Канал %1    Код до: %2    Код после: %3    Δкод: %4    %5\n")
                .arg(attribute(*value, "observed_channel").rightJustified(2, QLatin1Char(' ')),
                     number(numberAttribute(*value, "baseline_code"), 2),
                     number(numberAttribute(*value, "current_code"), 2),
                     signedNumber(delta, 2),
                     protocolVerdict(value->verdict));
        }
    }
    sectionResult(output, QStringLiteral("ПРОВЕРКА ЗАЩИТЫ АНАЛОГОВЫХ ВХОДОВ ОТ ПЕРЕГРУЗОК ±12 В"),
                  impacts.empty() ? tu::RunVerdict::NotRun : step.verdict);
}

void writeYalkReference(QString& output, const tu::StepRunResult& step)
{
    section(output, QStringLiteral("ПРОВЕРКА ВЫСОКОСТАБИЛЬНОГО ЭТАЛОННОГО НАПРЯЖЕНИЯ"),
            QStringLiteral("1.1.4.9"));
    for (const auto& value : step.measurements) {
        if (!hasKey(value, "ubsi.reference_6v2")) continue;
        output += QStringLiteral("Номинальное значение: %1 В\nДопустимое отклонение: ±%2 В\n"
                                 "Допустимый диапазон: %3...%4 В\n"
                                 "Значение эталонного напряжения в телеметрической информации: %5 В    %6\n")
            .arg(number(value.reference, 1), number(value.upperLimit-value.reference, 2),
                 number(value.lowerLimit), number(value.upperLimit), reportValue(value),
                 protocolVerdict(value.verdict));
    }
    sectionResult(output, QStringLiteral("ПРОВЕРКА ВЫСОКОСТАБИЛЬНОГО ЭТАЛОННОГО НАПРЯЖЕНИЯ"),
                  step.verdict);
}

void writeYvp(QString& output, const tu::StepRunResult& step)
{
    section(output, QStringLiteral("ПРОВЕРКА ОПРОСА И ПРЕОБРАЗОВАНИЯ ИНФОРМАЦИИ ОТ ВИБРОДАТЧИКОВ"),
            QStringLiteral("1.1.4.1, 1.1.4.7, 1.1.4.8, 1.1.4.14"));
    output += QStringLiteral("ПРОВЕРКА АЧХ\n");
    const auto grouped = byChannel(step, "ubsi.yvp.", "yvp_channel");
    tu::RunVerdict afcVerdict = tu::RunVerdict::Ok;
    bool hasAfc = false;
    for (const auto& [channel, values] : grouped) {
        std::vector<const tu::MeasurementResult*> afc;
        std::vector<const tu::MeasurementResult*> gain;
        const tu::MeasurementResult* attenuation = nullptr;
        for (const auto* value : values) {
            const QString criterion = attribute(*value, "criterion");
            if (criterion == QStringLiteral("afc")) afc.push_back(value);
            else if (criterion == QStringLiteral("gain")) gain.push_back(value);
            else if (criterion == QStringLiteral("attenuation")) attenuation = value;
        }
        if (afc.empty() && !attenuation) continue;
        hasAfc = true;
        output += QStringLiteral("\nПроверка канала №%1\n").arg(channel);
        tu::RunVerdict channelVerdict = tu::RunVerdict::Ok;
        if (!afc.empty())
            output += QStringLiteral("Частота =  500 Гц    Амплитуда = 1.0000    Отклонение = +0.00 %    НОРМА\n");
        std::sort(afc.begin(), afc.end(), [](const auto* lhs, const auto* rhs) {
            return numberAttribute(*lhs, "set_frequency_hz") < numberAttribute(*rhs, "set_frequency_hz");
        });
        for (const auto* value : afc) {
            const double deviation = reportMeasured(*value);
            output += QStringLiteral("Частота = %1 Гц    Амплитуда = %2    Отклонение = %3 %    %4\n")
                .arg(number(numberAttribute(*value, "set_frequency_hz"), 0).rightJustified(4, QLatin1Char(' ')),
                     number(1.0 + deviation / 100.0, 4), signedNumber(deviation, 2),
                     protocolVerdict(value->verdict));
            channelVerdict = tu::combineVerdicts(channelVerdict, value->verdict);
        }
        if (attenuation) {
            const double attenuationDb = reportMeasured(*attenuation);
            const double ratio = std::pow(10.0, -attenuationDb / 20.0);
            output += QStringLiteral("Частота = 4000 Гц    Амплитуда = %1    Затухание относительно 500 Гц = %2 дБ    %3\n")
                .arg(number(ratio, 4), reportValue(*attenuation, 2),
                     protocolVerdict(attenuation->verdict));
            channelVerdict = tu::combineVerdicts(channelVerdict, attenuation->verdict);
        }
        output += QStringLiteral("Проверка АЧХ канала №%1    %2\n")
            .arg(channel).arg(protocolVerdict(channelVerdict));
        afcVerdict = tu::combineVerdicts(afcVerdict, channelVerdict);
    }
    sectionResult(output, QStringLiteral("ПРОВЕРКА АЧХ КАНАЛОВ ЯЧЕЙКИ ЯВП"),
                  hasAfc ? afcVerdict : tu::RunVerdict::NotRun);

    output += QStringLiteral("\nПРОВЕРКА КОЭФФИЦИЕНТОВ УСИЛЕНИЯ\n");
    tu::RunVerdict gainVerdict = tu::RunVerdict::Ok;
    bool hasGain = false;
    for (const auto& [channel, values] : grouped) {
        std::vector<const tu::MeasurementResult*> gain;
        for (const auto* value : values)
            if (attribute(*value, "criterion") == QStringLiteral("gain")) gain.push_back(value);
        if (gain.empty()) continue;
        hasGain = true;
        output += QStringLiteral("\nПроверка канала №%1\n").arg(channel);
        tu::RunVerdict channelVerdict = tu::RunVerdict::Ok;
        std::sort(gain.begin(), gain.end(), [](const auto* lhs, const auto* rhs) {
            return lhs->reference < rhs->reference;
        });
        for (const auto* value : gain) {
            const QString error = reportError(*value);
            output += QStringLiteral("Коэффициент усиления %1 мВ/пКл = %2 мВ/пКл    Отклонение = %3    %4\n")
                .arg(number(value->reference, 2), reportValue(*value, 4),
                     error.isEmpty() ? QStringLiteral("—") : error,
                     protocolVerdict(value->verdict));
            channelVerdict = tu::combineVerdicts(channelVerdict, value->verdict);
        }
        output += QStringLiteral("Проверка коэффициентов усиления канала №%1    %2\n")
            .arg(channel).arg(protocolVerdict(channelVerdict));
        gainVerdict = tu::combineVerdicts(gainVerdict, channelVerdict);
    }
    sectionResult(output, QStringLiteral("ПРОВЕРКА КОЭФФИЦИЕНТОВ УСИЛЕНИЯ ЯВП"),
                  hasGain ? gainVerdict : tu::RunVerdict::NotRun);
    sectionResult(output, QStringLiteral("ПРОВЕРКА ОПРОСА И ПРЕОБРАЗОВАНИЯ ИНФОРМАЦИИ ОТ ВИБРОДАТЧИКОВ"), step.verdict);
}

double maximumReportedDeviation(const tu::StepRunResult& step, const char* key)
{
    double maximum = std::numeric_limits<double>::quiet_NaN();
    for (const auto& value : step.measurements) {
        if (!hasKey(value, key) || isRawEngineeringMeasurement(value)) continue;
        double current = numberAttribute(value, "report_deviation_percent");
        if (!std::isfinite(current)) current = numberAttribute(value, "reduced_error_percent");
        if (!std::isfinite(current)) current = numberAttribute(value, "gain_error_percent");
        if (std::isfinite(current)) maximum = std::isfinite(maximum)
            ? std::max(maximum, std::abs(current)) : std::abs(current);
    }
    return maximum;
}

void writeAccuracy(QString& output, const tu::ScenarioRunResult& result)
{
    const auto* yalk = findStep(result.steps, "yalk_channels");
    const auto* ytp = findStep(result.steps, "ytp_channels");
    const auto* yvp = findStep(result.steps, "yvp_channels");
    if (!yalk && !ytp && !yvp) return;

    section(output, QStringLiteral("ПРОВЕРКА ПОГРЕШНОСТЕЙ ОТ ШКАЛЫ ИЗМЕРЕНИЙ"),
            QStringLiteral("1.1.4.14"));
    if (yalk) {
        output += QStringLiteral("Аналоговые медленноменяющиеся параметры: максимальная погрешность %1 %    %2\n")
            .arg(number(maximumReportedDeviation(*yalk, "ubsi.yalk.channel."), 2),
                 protocolVerdict(yalk->verdict));
    }
    if (ytp) {
        output += QStringLiteral("Температурные параметры: максимальная погрешность %1 %    %2\n")
            .arg(number(maximumReportedDeviation(*ytp, "ubsi.ytp.channel."), 2),
                 protocolVerdict(ytp->verdict));
    }
    if (yvp) {
        output += QStringLiteral("Быстроменяющиеся параметры: максимальное отклонение %1 %    %2\n")
            .arg(number(maximumReportedDeviation(*yvp, "ubsi.yvp."), 2),
                 protocolVerdict(yvp->verdict));
    }
    sectionResult(output, QStringLiteral("ПРОВЕРКА ПОГРЕШНОСТЕЙ ОТ ШКАЛЫ ИЗМЕРЕНИЙ"), result.verdict);
}

QString makeProtocolText(const tu::ScenarioRunResult& result)
{
    const QString title = QStringLiteral("РЕЗУЛЬТАТЫ ПРОВЕРКИ УБСИ №%1 В НОРМАЛЬНЫХ УСЛОВИЯХ")
        .arg(text(result.objectSerial));
    QString output = title + QStringLiteral("\n\nДата: %1\nОператор: не указан\n")
        .arg(QDateTime::fromMSecsSinceEpoch(
            std::chrono::duration_cast<std::chrono::milliseconds>(result.finishedAt.time_since_epoch()).count())
            .toString(QStringLiteral("dd.MM.yyyy")));

    if (const auto* step = findStep(result.steps, "readiness")) writeReadiness(output, *step);
    if (const auto* step = findStep(result.steps, "supply_range")) writePower(output, *step);
    if (const auto* step = findStep(result.steps, "yalk_channels")) writeYalkAnalog(output, *step);
    if (const auto* step = findStep(result.steps, "yalk_reference_voltage")) writeYalkReference(output, *step);
    if (const auto* step = findStep(result.steps, "yalk_initial")) writeYalkInitial(output, *step);
    if (const auto* step = findStep(result.steps, "yalk_overload")) writeYalkOverload(output, *step);
    if (const auto* step = findStep(result.steps, "yalk_channels")) writeYalkContact(output, *step);
    if (const auto* step = findStep(result.steps, "ytp_channels")) writeYtp(output, *step);
    if (const auto* step = findStep(result.steps, "yvp_channels")) writeYvp(output, *step);
    writeAccuracy(output, result);

    divider(output);
    output += QStringLiteral("РЕЗУЛЬТАТЫ ПРОВЕРКИ УБСИ В НОРМАЛЬНЫХ УСЛОВИЯХ    %1\n")
        .arg(protocolVerdict(result.verdict));
    return output;
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
    const QString textPath = QDir(directory).filePath(base + QStringLiteral(".txt"));
    const QString csvPath = QDir(directory).filePath(base + QStringLiteral(".csv"));

    const QString protocol = makeProtocolText(result);

    QSaveFile textFile(textPath);
    if (!textFile.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error(QStringLiteral("Не удалось открыть текстовый отчёт: %1")
            .arg(textPath).toUtf8().toStdString());
    textFile.write(protocol.toUtf8());
    if (!textFile.commit())
        throw std::runtime_error(QStringLiteral("Не удалось сохранить текстовый отчёт: %1")
            .arg(textPath).toUtf8().toStdString());

    QSaveFile htmlFile(htmlPath);
    if (!htmlFile.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error(QStringLiteral("Не удалось открыть отчёт: %1").arg(htmlPath).toUtf8().toStdString());
    QTextStream out(&htmlFile);
    out.setEncoding(QStringConverter::Utf8);
    out << QStringLiteral("<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\"><title>")
        << QStringLiteral("Результаты проверки УБСИ").toHtmlEscaped()
        << QStringLiteral("</title><style>@page{size:A4;margin:16mm}body{margin:0;color:#111;font-family:'Courier New',monospace;font-size:10pt;line-height:1.35}pre{margin:0;white-space:pre-wrap;word-break:normal}@media print{body{font-size:9pt}}</style></head><body><pre>")
        << protocol.toHtmlEscaped() << QStringLiteral("</pre></body></html>");
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

void updateTuReportOperator(const QString& htmlPath, const QString& operatorName)
{
    const QString normalized = operatorName.trimmed();
    if (htmlPath.isEmpty() || normalized.isEmpty()) return;

    const QFileInfo htmlInfo(htmlPath);
    const QString textPath = htmlInfo.dir().filePath(htmlInfo.completeBaseName() + QStringLiteral(".txt"));
    const QString from = QStringLiteral("Оператор: не указан");
    const QString to = QStringLiteral("Оператор: %1").arg(normalized);

    for (const QString& path : {textPath, htmlPath}) {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QString contents = QString::fromUtf8(input.readAll());
        input.close();
        if (!contents.contains(from)) continue;
        contents.replace(from, to.toHtmlEscaped());
        if (path == textPath) contents.replace(to.toHtmlEscaped(), to);

        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) continue;
        output.write(contents.toUtf8());
        output.commit();
    }
}
