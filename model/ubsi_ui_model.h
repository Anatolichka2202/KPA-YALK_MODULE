#pragma once

#include "model/run_types.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace ubsi::ui {

enum class VerificationState { Pending, Norma, NeNorma, Incomplete, Error, Stopped };
enum class RuntimeState { Idle, Preparing, Running, WaitingOperator, StandError, Stopped, Finished };
enum class Procedure { Preparation, Power, YalkInitial, YalkAnalog, YalkOverload, YalkReference, Ytp, Yvp, Finish };

inline VerificationState verificationFromVerdict(tu::RunVerdict verdict)
{
    using V = tu::RunVerdict;
    switch (verdict) {
    case V::Ok: return VerificationState::Norma;
    case V::Fail: return VerificationState::NeNorma;
    case V::Incomplete: return VerificationState::Incomplete;
    case V::Error: return VerificationState::Error;
    case V::Aborted: return VerificationState::Stopped;
    case V::NotRun: return VerificationState::Pending;
    }
    return VerificationState::Pending;
}

inline QString verificationText(VerificationState state)
{
    switch (state) {
    case VerificationState::Norma: return QStringLiteral("НОРМА");
    case VerificationState::NeNorma: return QStringLiteral("НЕ НОРМА");
    case VerificationState::Incomplete: return QStringLiteral("НЕПОЛНАЯ");
    case VerificationState::Error: return QStringLiteral("ОШИБКА");
    case VerificationState::Stopped: return QStringLiteral("ОСТАНОВЛЕНО");
    case VerificationState::Pending: return QStringLiteral("—");
    }
    return QStringLiteral("—");
}

inline QString eventValue(const tu::RunEvent& event, const char* key)
{
    const auto found = event.data.find(key);
    return found == event.data.end() ? QString() : QString::fromStdString(found->second);
}

inline double eventDouble(const tu::RunEvent& event, const char* key,
                          double fallback = std::numeric_limits<double>::quiet_NaN())
{
    bool ok = false;
    const double value = eventValue(event, key).toDouble(&ok);
    return ok && std::isfinite(value) ? value : fallback;
}

inline int eventInt(const tu::RunEvent& event, const char* key, int fallback = 0)
{
    bool ok = false;
    const int value = eventValue(event, key).toInt(&ok);
    return ok ? value : fallback;
}

inline bool eventBool(const tu::RunEvent& event, const char* key, bool fallback = false)
{
    const QString value = eventValue(event, key).trimmed().toLower();
    if (value.isEmpty()) return fallback;
    return value == QStringLiteral("true") || value == QStringLiteral("1")
        || value == QStringLiteral("yes") || value == QStringLiteral("on");
}

inline QVector<double> csvNumbers(const QString& text)
{
    QVector<double> result;
    for (const auto& token : text.split(',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const double value = token.trimmed().toDouble(&ok);
        if (ok && std::isfinite(value)) result.push_back(value);
    }
    return result;
}

// Background monitor frames are positional arrays: an unavailable element must
// not shift every channel that follows it. Preserve array length and use NaN as
// the missing-value marker.
inline QVector<double> csvNumbersWithGaps(const QString& text)
{
    QVector<double> result;
    if (text.isEmpty()) return result;
    for (const auto& token : text.split(',', Qt::KeepEmptyParts)) {
        bool ok = false;
        const double value = token.trimmed().toDouble(&ok);
        result.push_back(ok && std::isfinite(value)
            ? value : std::numeric_limits<double>::quiet_NaN());
    }
    return result;
}

inline QVector<int> yalkPhysicalAddresses()
{
    QVector<int> keys;
    keys.reserve(80);
    for (int address = 1; address <= 87; ++address) {
        if (address <= 28 || (address >= 32 && address <= 43)
            || (address >= 45 && address <= 70) || address >= 74) {
            keys.push_back(address);
        }
    }
    return keys;
}

struct RunUiState {
    QString productSerial;
    QString operatorName;
    QString productionStage;
    QString scope;
    RuntimeState runtimeState = RuntimeState::Idle;
    Procedure currentProcedure = Procedure::Preparation;
    qint64 elapsedMs = 0;
    VerificationState productVerdict = VerificationState::Pending;
    QString operatorComment;
    QString procedureContext;
    QString progressText;
};

struct CurrentTelemetry {
    double totalCurrentA = std::numeric_limits<double>::quiet_NaN();
    bool fresh = false;
    QVector<double> samples;
};

struct PowerUiState {
    int sequenceIndex = 0;
    int sequenceCount = 7;
    double setpointV = std::numeric_limits<double>::quiet_NaN();
    double actualV = std::numeric_limits<double>::quiet_NaN();
    qint64 holdElapsedMs = 0;
    qint64 holdDurationMs = 0;
    VerificationState stepResult = VerificationState::Pending;
    QVector<double> actualHistory;
    QVector<double> setpointHistory;
    QVector<double> passiveYalk;
    bool passiveYalkFresh = false;
};

struct InitialChannel {
    int physicalAddress = 0;
    double analogV = std::numeric_limits<double>::quiet_NaN();
    int logic = -1;
    int expectedLogic = -1;
    VerificationState verification = VerificationState::Pending;
};

struct AnalogChannel {
    int physicalAddress = 0;
    double rawCode = std::numeric_limits<double>::quiet_NaN();
    double currentV = std::numeric_limits<double>::quiet_NaN();
    double minimumV = std::numeric_limits<double>::quiet_NaN();
    double maximumV = std::numeric_limits<double>::quiet_NaN();
    double reducedErrorPercent = std::numeric_limits<double>::quiet_NaN();
    VerificationState verification = VerificationState::Pending;
    int contactLogic = -1;
    int expectedContactLogic = -1;
    VerificationState contactVerification = VerificationState::Pending;
    bool warning = false;
};

struct YalkAnalogFrame {
    double pointV = std::numeric_limits<double>::quiet_NaN();
    double actualReferenceV7 = std::numeric_limits<double>::quiet_NaN();
    int stimulatedChannel = 0;
    int pinnedChannel = 0;
    QVector<AnalogChannel> channels;
};

struct ContactChannel : AnalogChannel { int logic = -1; };
struct YalkContactFrame {
    double pointV = std::numeric_limits<double>::quiet_NaN();
    double actualReferenceV7 = std::numeric_limits<double>::quiet_NaN();
    int expectedLogic = -1;
    int stimulatedChannel = 0;
    int pinnedChannel = 0;
    QVector<ContactChannel> channels;
};

struct OverloadChannel {
    int physicalAddress = 0;
    double baselineCode = std::numeric_limits<double>::quiet_NaN();
    double currentCode = std::numeric_limits<double>::quiet_NaN();
    double deltaCode = std::numeric_limits<double>::quiet_NaN();
    VerificationState verification = VerificationState::Pending;
};
struct YalkOverloadFrame {
    QString polarity;
    int stressedChannel = 0;
    int impactIndex = 0;
    int impactCount = 160;
    qint64 holdElapsedMs = 0;
    qint64 holdDurationMs = 10000;
    double criterionCode = 2.0;
    QVector<OverloadChannel> channels;
};

struct YtpChannel {
    int channel = 0;
    double currentOhm = std::numeric_limits<double>::quiet_NaN();
    double minimumOhm = std::numeric_limits<double>::quiet_NaN();
    double maximumOhm = std::numeric_limits<double>::quiet_NaN();
    VerificationState verification = VerificationState::Pending;
};
struct YtpFrame {
    double resistancePointOhm = std::numeric_limits<double>::quiet_NaN();
    double actualReferenceOhm = std::numeric_limits<double>::quiet_NaN();
    int testedChannel = 0;
    int pinnedChannel = 0;
    bool waitingOperator = false;
    int pointIndex = 0;
    int pointCount = 3;
    QVector<YtpChannel> channels;
};

struct YvpChannel {
    int channel = 0;
    double stimulusValue = std::numeric_limits<double>::quiet_NaN();
    double measuredValue = std::numeric_limits<double>::quiet_NaN();
    double calculatedGain = std::numeric_limits<double>::quiet_NaN();
    VerificationState verification = VerificationState::Pending;
};
struct YvpFrame {
    int testedChannel = 0;
    int pinnedChannel = 0;
    QString unit = QStringLiteral("мВ/пКл");
    double gain = std::numeric_limits<double>::quiet_NaN();
    double frequencyHz = std::numeric_limits<double>::quiet_NaN();
    int pointIndex = 0;
    int pointCount = 144;
    bool acceptanceApplied = false;
    QVector<YvpChannel> channels;
};

struct SectionSummary {
    VerificationState verdict = VerificationState::Pending;
    int stepCount = 0;
};

class UiAdapter final {
public:
    UiAdapter() { reset(); }

    void reset()
    {
        run = {};
        telemetry = {};
        power = {};
        initial.clear();
        yalkAnalog = {};
        yalkContact = {};
        yalkOverload = {};
        ytp = {};
        yvp = {};
        summaries.fill({});

        const auto addresses = yalkPhysicalAddresses();
        for (int address : addresses) {
            initial.push_back({address});
            AnalogChannel a; a.physicalAddress = address; yalkAnalog.channels.push_back(a);
            ContactChannel c; c.physicalAddress = address; yalkContact.channels.push_back(c);
        }
        for (int address : addresses) {
            OverloadChannel c; c.physicalAddress = address; yalkOverload.channels.push_back(c);
        }
        for (int channel = 1; channel <= 30; ++channel) {
            YtpChannel c; c.channel = channel; ytp.channels.push_back(c);
        }
        for (int channel = 1; channel <= 8; ++channel) {
            YvpChannel c; c.channel = channel; yvp.channels.push_back(c);
        }
    }

    void apply(const tu::RunEvent& event)
    {
        const QString node = QString::fromStdString(event.nodeId);
        const QString stage = QString::fromStdString(event.stage);
        if (stage == QStringLiteral("ISD_PAUSE")) {
            run.runtimeState = RuntimeState::WaitingOperator;
            run.progressText = QString::fromStdString(event.message);
            return;
        }
        if (stage == QStringLiteral("ISD_RESUMED")) {
            run.runtimeState = RuntimeState::Running;
            run.progressText = QString::fromStdString(event.message);
            return;
        }
        if (stage != QStringLiteral("BACKGROUND")
            || run.runtimeState != RuntimeState::WaitingOperator)
            run.runtimeState = RuntimeState::Running;
        mapProcedure(node, stage);

        const double current = eventDouble(event, "amperes");
        if (std::isfinite(current)) {
            telemetry.totalCurrentA = current;
            telemetry.fresh = true;
            telemetry.samples.push_back(current);
            while (telemetry.samples.size() > 240) telemetry.samples.removeFirst();
        }

        if (node == QStringLiteral("supply_range") || node == QStringLiteral("supply_status")) {
            applyPower(event);
        }
        if (stage == QStringLiteral("POWER_YALK")) applyPowerYalk(event);
        if (stage == QStringLiteral("BACKGROUND")) {
            const QString section = eventValue(event, "section");
            if (section == QStringLiteral("YALK")) applyYalkBackground(event);
            else if (section == QStringLiteral("YTP")) applyYtpBackground(event);
        }
        if (node.contains(QStringLiteral("yalk_initial")) || stage == QStringLiteral("YALK_INITIAL"))
            applyYalkInitial(event);
        if (node == QStringLiteral("yalk_channels") && stage == QStringLiteral("MEASUREMENT")) {
            if (eventValue(event, "signal").isEmpty()) {
                applyYalkAnalog(event);
            } else {
                applyYalkContact(event);
            }
        }
        if (node.contains(QStringLiteral("yalk_contact")) && stage == QStringLiteral("MEASUREMENT"))
            applyYalkContact(event);
        if (node.contains(QStringLiteral("yalk_overload"))) applyYalkOverload(event);
        if (node.contains(QStringLiteral("ytp_"))) applyYtp(event);
        if (node.contains(QStringLiteral("yvp_")) || stage == QStringLiteral("YVP_V7_POINT"))
            applyYvp(event);
    }

    void applyResult(const tu::ScenarioRunResult& result)
    {
        run.productVerdict = verificationFromVerdict(result.verdict);
        run.runtimeState = result.verdict == tu::RunVerdict::Aborted
            ? RuntimeState::Stopped : RuntimeState::Finished;
        run.currentProcedure = Procedure::Finish;
        summaries.fill({});
        for (const auto& step : result.steps) collectResult(step);
    }

    RunUiState run;
    CurrentTelemetry telemetry;
    PowerUiState power;
    QVector<InitialChannel> initial;
    YalkAnalogFrame yalkAnalog;
    YalkContactFrame yalkContact;
    YalkOverloadFrame yalkOverload;
    YtpFrame ytp;
    YvpFrame yvp;
    std::array<SectionSummary, 4> summaries;

private:
    template <typename Channel>
    static int findAddress(const QVector<Channel>& channels, int address)
    {
        for (int i = 0; i < channels.size(); ++i) if (channels[i].physicalAddress == address) return i;
        return -1;
    }

    void mapProcedure(const QString& node, const QString& stage)
    {
        if (node.startsWith(QStringLiteral("supply_"))) {
            run.currentProcedure = Procedure::Power;
            run.procedureContext = QStringLiteral("Питание");
        } else if (node.contains(QStringLiteral("yalk_initial")) || stage == QStringLiteral("YALK_INITIAL")) {
            run.currentProcedure = Procedure::YalkInitial;
            run.procedureContext = QStringLiteral("Обрыв / исходное состояние");
        } else if (node == QStringLiteral("yalk_channels")
                   || node.contains(QStringLiteral("yalk_contact"))) {
            run.currentProcedure = Procedure::YalkAnalog;
            run.procedureContext = QStringLiteral("Аналоговые и контактные каналы");
        } else if (node.contains(QStringLiteral("yalk_overload"))) {
            run.currentProcedure = Procedure::YalkOverload;
            run.procedureContext = QStringLiteral("Перегрузка ±12 В");
        } else if (node.contains(QStringLiteral("yalk_reference"))) {
            run.currentProcedure = Procedure::YalkReference;
            run.procedureContext = QStringLiteral("Эталон 6,2 В");
        } else if (node.startsWith(QStringLiteral("ytp_"))) {
            run.currentProcedure = Procedure::Ytp;
            run.procedureContext = QStringLiteral("ЯТП");
        } else if (node.startsWith(QStringLiteral("yvp_")) || stage == QStringLiteral("YVP_V7_POINT")) {
            run.currentProcedure = Procedure::Yvp;
            run.procedureContext = QStringLiteral("ЯВП-8");
        }
    }

    void applyPower(const tu::RunEvent& event)
    {
        const double setpoint = eventDouble(event, "setpoint_v");
        const double actual = eventDouble(event, "volts", eventDouble(event, "actual_v"));
        if (std::isfinite(setpoint)) {
            power.setpointV = setpoint;
            power.setpointHistory.push_back(setpoint);
            while (power.setpointHistory.size() > 240) power.setpointHistory.removeFirst();
            static const std::array<double, 7> sequence = {24, 27, 35, 19, 27, 37, 27};
            for (int i = 0; i < static_cast<int>(sequence.size()); ++i)
                if (std::abs(setpoint - sequence[i]) < 0.1) power.sequenceIndex = i + 1;
        }
        if (std::isfinite(actual)) {
            power.actualV = actual;
            power.actualHistory.push_back(actual);
            while (power.actualHistory.size() > 240) power.actualHistory.removeFirst();
        }
        power.holdElapsedMs = qRound64(eventDouble(event, "elapsed_s", 0.0) * 1000.0);
        power.holdDurationMs = qRound64(eventDouble(event, "duration_s", 0.0) * 1000.0);
        power.stepResult = verificationFromVerdict(event.verdict);
        run.progressText = power.holdDurationMs > 0
            ? QStringLiteral("Выдержка %1 / %2 с")
                .arg(power.holdElapsedMs / 1000.0, 0, 'f', 1)
                .arg(power.holdDurationMs / 1000.0, 0, 'f', 1)
            : QStringLiteral("Шаг %1 / %2").arg(power.sequenceIndex).arg(power.sequenceCount);
    }

    void applyPowerYalk(const tu::RunEvent& event)
    {
        power.passiveYalkFresh = eventBool(event, "fresh");
        if (power.passiveYalkFresh) power.passiveYalk = csvNumbers(eventValue(event, "values_v"));
    }

    void applyYalkBackground(const tu::RunEvent& event)
    {
        const auto mean = csvNumbersWithGaps(eventValue(event, "background_mean"));
        const auto minimum = csvNumbersWithGaps(eventValue(event, "background_min"));
        const auto maximum = csvNumbersWithGaps(eventValue(event, "background_max"));
        const auto codes = csvNumbersWithGaps(eventValue(event, "background_codes"));
        const auto contacts = csvNumbersWithGaps(eventValue(event, "background_contacts"));
        const auto addresses = yalkPhysicalAddresses();
        for (int i = 0; i < addresses.size(); ++i) {
            const int source = addresses[i] - 1;
            if (source < mean.size() && std::isfinite(mean[source])) {
                yalkAnalog.channels[i].currentV = mean[source];
                yalkContact.channels[i].currentV = mean[source];
            }
            if (source < minimum.size() && std::isfinite(minimum[source])) {
                yalkAnalog.channels[i].minimumV = minimum[source];
                yalkContact.channels[i].minimumV = minimum[source];
            }
            if (source < maximum.size() && std::isfinite(maximum[source])) {
                yalkAnalog.channels[i].maximumV = maximum[source];
                yalkContact.channels[i].maximumV = maximum[source];
            }
            if (source < codes.size() && std::isfinite(codes[source]))
                yalkAnalog.channels[i].rawCode = codes[source];
            if (source < contacts.size() && std::isfinite(contacts[source]))
                yalkAnalog.channels[i].contactLogic = contacts[source] >= 0.5 ? 1 : 0;
        }
    }

    void applyYtpBackground(const tu::RunEvent& event)
    {
        const auto mean = csvNumbersWithGaps(eventValue(event, "background_mean"));
        const auto minimum = csvNumbersWithGaps(eventValue(event, "background_min"));
        const auto maximum = csvNumbersWithGaps(eventValue(event, "background_max"));
        for (int i = 0; i < ytp.channels.size(); ++i) {
            if (i < mean.size() && std::isfinite(mean[i])) ytp.channels[i].currentOhm = mean[i];
            if (i < minimum.size() && std::isfinite(minimum[i])) ytp.channels[i].minimumOhm = minimum[i];
            if (i < maximum.size() && std::isfinite(maximum[i])) ytp.channels[i].maximumOhm = maximum[i];
        }
    }

    void applyYalkInitial(const tu::RunEvent& event)
    {
        const int address = eventInt(event, "ulk_address");
        if (eventValue(event, "contact_mode") == QStringLiteral("Разомкнуто")) {
            const int index = findAddress(yalkAnalog.channels, address);
            if (index >= 0) {
                yalkAnalog.channels[index].contactLogic = eventInt(event, "signal", -1);
                yalkAnalog.channels[index].expectedContactLogic = 1;
                yalkAnalog.channels[index].contactVerification = verificationFromVerdict(event.verdict);
            }
            return;
        }
        for (auto& channel : initial) {
            if (channel.physicalAddress != address) continue;
            channel.analogV = eventDouble(event, "yalk_v");
            channel.logic = eventInt(event, "signal", -1);
            channel.expectedLogic = eventInt(event, "expected_signal", -1);
            channel.verification = verificationFromVerdict(event.verdict);
            break;
        }
        const int index = eventInt(event, "channel_index");
        const int count = eventInt(event, "channel_count", 80);
        run.progressText = QStringLiteral("Обрыв · вход %1 / %2 · адрес %3")
            .arg(index > 0 ? index : 0).arg(count).arg(address);
    }

    void applyYalkAnalog(const tu::RunEvent& event)
    {
        const int address = eventInt(event, "ulk_address");
        const int index = findAddress(yalkAnalog.channels, address);
        if (index < 0) return;
        auto samples = csvNumbers(eventValue(event, "value_samples"));
        double minimum = eventDouble(event, "yalk_v");
        double maximum = minimum;
        if (!samples.isEmpty()) {
            const auto mm = std::minmax_element(samples.begin(), samples.end());
            minimum = *mm.first;
            maximum = *mm.second;
        }
        auto& channel = yalkAnalog.channels[index];
        channel.currentV = eventDouble(event, "yalk_v");
        channel.minimumV = minimum;
        channel.maximumV = maximum;
        channel.verification = verificationFromVerdict(event.verdict);
        channel.reducedErrorPercent = eventDouble(event, "reduced_error_percent");
        channel.rawCode = eventDouble(event, "analog_code");
        channel.warning = false;
        yalkAnalog.pointV = eventDouble(event, "command_v");
        yalkAnalog.actualReferenceV7 = eventDouble(event, "v7_v");
        yalkAnalog.stimulatedChannel = address;
        run.progressText = QStringLiteral("Канал %1 / 80 · адрес %2")
            .arg(index + 1).arg(address);
    }

    void applyYalkContact(const tu::RunEvent& event)
    {
        const int address = eventInt(event, "ulk_address");
        const int index = findAddress(yalkContact.channels, address);
        if (index < 0) return;
        auto samples = csvNumbers(eventValue(event, "value_samples"));
        double minimum = eventDouble(event, "yalk_v");
        double maximum = minimum;
        if (!samples.isEmpty()) {
            const auto mm = std::minmax_element(samples.begin(), samples.end());
            minimum = *mm.first;
            maximum = *mm.second;
        }
        auto& channel = yalkContact.channels[index];
        channel.currentV = eventDouble(event, "yalk_v");
        channel.minimumV = minimum;
        channel.maximumV = maximum;
        channel.logic = eventInt(event, "signal", -1);
        channel.verification = verificationFromVerdict(event.verdict);
        yalkContact.pointV = eventDouble(event, "command_v");
        yalkContact.actualReferenceV7 = eventDouble(event, "v7_v");
        yalkContact.expectedLogic = eventInt(event, "expected_signal", yalkContact.pointV >= 2.0 ? 1 : 0);
        yalkContact.stimulatedChannel = address;
        auto& combined = yalkAnalog.channels[index];
        combined.currentV = channel.currentV;
        combined.minimumV = channel.minimumV;
        combined.maximumV = channel.maximumV;
        combined.contactLogic = channel.logic;
        combined.expectedContactLogic = yalkContact.expectedLogic;
        combined.contactVerification = channel.verification;
        yalkAnalog.pointV = yalkContact.pointV;
        yalkAnalog.actualReferenceV7 = yalkContact.actualReferenceV7;
        yalkAnalog.stimulatedChannel = address;
        run.progressText = QStringLiteral("Точка %1 В · канал %2 / 80")
            .arg(yalkContact.pointV, 0, 'f', 1).arg(index + 1);
    }

    void applyYalkOverload(const tu::RunEvent& event)
    {
        yalkOverload.polarity = eventValue(event, "polarity");
        yalkOverload.stressedChannel = eventInt(event, "stressed_channel", yalkOverload.stressedChannel);
        yalkOverload.impactIndex = eventInt(event, "impact_index", yalkOverload.impactIndex);
        yalkOverload.impactCount = eventInt(event, "impact_count", yalkOverload.impactCount);
        yalkOverload.holdDurationMs = eventInt(event, "settle_ms", static_cast<int>(yalkOverload.holdDurationMs));
        if (QString::fromStdString(event.stage) == QStringLiteral("MEASUREMENT")) {
            const int observed = eventInt(event, "observed_channel");
            const int index = findAddress(yalkOverload.channels, observed);
            if (index >= 0) {
                auto& channel = yalkOverload.channels[index];
                channel.baselineCode = eventDouble(event, "baseline_code");
                channel.currentCode = eventDouble(event, "current_code");
                channel.deltaCode = eventDouble(event, "delta_code");
                channel.verification = verificationFromVerdict(event.verdict);
                yalkOverload.criterionCode = std::max(std::abs(eventDouble(event, "lower_delta_code", -2.0)),
                                                      std::abs(eventDouble(event, "upper_delta_code", 2.0)));
            }
        }
        run.progressText = QStringLiteral("Воздействие %1 / %2 · канал %3 · %4")
            .arg(yalkOverload.impactIndex).arg(yalkOverload.impactCount)
            .arg(yalkOverload.stressedChannel)
            .arg(yalkOverload.polarity.isEmpty() ? QStringLiteral("±12 В") : yalkOverload.polarity);
    }

    void applyYtp(const tu::RunEvent& event)
    {
        const QString stage = QString::fromStdString(event.stage);
        if (stage == QStringLiteral("OPERATOR")) {
            ytp.waitingOperator = true;
            ytp.resistancePointOhm = eventDouble(event, "target_resistance_ohm");
            ytp.pointIndex = eventInt(event, "point_index");
            ytp.pointCount = eventInt(event, "point_count", 3);
            run.runtimeState = RuntimeState::WaitingOperator;
            run.progressText = QStringLiteral("Установите Р4831: %1 Ом")
                .arg(ytp.resistancePointOhm, 0, 'f', 0);
            return;
        }
        if (stage != QStringLiteral("MEASUREMENT")) return;
        ytp.waitingOperator = false;
        run.runtimeState = RuntimeState::Running;
        const int channelNumber = eventInt(event, "ytp_channel");
        if (channelNumber < 1 || channelNumber > ytp.channels.size()) return;
        auto& channel = ytp.channels[channelNumber - 1];
        auto samples = csvNumbers(eventValue(event, "value_samples"));
        channel.currentOhm = eventDouble(event, "measured_resistance_ohm");
        channel.minimumOhm = channel.maximumOhm = channel.currentOhm;
        if (!samples.isEmpty()) {
            const auto mm = std::minmax_element(samples.begin(), samples.end());
            channel.minimumOhm = *mm.first;
            channel.maximumOhm = *mm.second;
        }
        channel.verification = verificationFromVerdict(event.verdict);
        ytp.actualReferenceOhm = eventDouble(event, "actual_reference_ohm");
        ytp.resistancePointOhm = std::isfinite(ytp.actualReferenceOhm)
            ? ytp.actualReferenceOhm : eventDouble(event, "target_resistance_ohm");
        ytp.testedChannel = channelNumber;
        run.progressText = QStringLiteral("Точка %1 Ом · канал %2 / 30")
            .arg(ytp.resistancePointOhm, 0, 'f', 0).arg(channelNumber);
    }

    void applyYvp(const tu::RunEvent& event)
    {
        const QString stage = QString::fromStdString(event.stage);
        if (stage != QStringLiteral("YVP_V7_POINT")
                && stage != QStringLiteral("YVP_CRITERION")) return;
        const int channelNumber = eventInt(event, "yvp_channel");
        if (channelNumber < 1 || channelNumber > yvp.channels.size()) return;
        auto& channel = yvp.channels[channelNumber - 1];
        yvp.testedChannel = channelNumber;
        if (stage == QStringLiteral("YVP_CRITERION")) {
            channel.verification = verificationFromVerdict(event.verdict);
            return;
        }
        yvp.gain = eventDouble(event, "gain_mv_per_pc");
        yvp.frequencyHz = eventDouble(event, "set_frequency_hz");
        channel.stimulusValue = yvp.gain;
        channel.measuredValue = eventDouble(event, "v7_output_vrms");
        channel.calculatedGain = eventDouble(event, "calculated_gain_mv_per_pc");
        channel.verification = verificationFromVerdict(event.verdict);
        yvp.acceptanceApplied = eventValue(event, "acceptance") != QStringLiteral("not_applied");
        yvp.pointIndex = std::max(yvp.pointIndex, eventInt(event, "point_index"));
        yvp.pointCount = std::max(yvp.pointCount, eventInt(event, "point_count", 144));
        run.progressText = QStringLiteral("Канал %1 / 8 · Kу %2 · %3 Гц")
            .arg(channelNumber)
            .arg(yvp.gain, 0, 'g', 6)
            .arg(yvp.frequencyHz, 0, 'g', 8);
    }

    static int summaryIndex(const QString& id)
    {
        if (id == QStringLiteral("readiness") || id.startsWith(QStringLiteral("supply_"))
            || id == QStringLiteral("power_off")) return 0;
        if (id.startsWith(QStringLiteral("yalk_"))) return 1;
        if (id.startsWith(QStringLiteral("ytp_"))) return 2;
        if (id.startsWith(QStringLiteral("yvp_"))) return 3;
        return -1;
    }

    void collectResult(const tu::StepRunResult& step)
    {
        if (!step.children.empty()) {
            for (const auto& child : step.children) collectResult(child);
            return;
        }
        const int index = summaryIndex(QString::fromStdString(step.nodeId));
        if (index < 0) return;
        auto& summary = summaries[static_cast<std::size_t>(index)];
        ++summary.stepCount;
        const auto current = verificationFromVerdict(step.verdict);
        auto rank = [](VerificationState state) {
            switch (state) {
            case VerificationState::Error: return 6;
            case VerificationState::NeNorma: return 5;
            case VerificationState::Stopped: return 4;
            case VerificationState::Incomplete: return 3;
            case VerificationState::Norma: return 2;
            case VerificationState::Pending: return 1;
            }
            return 0;
        };
        if (rank(current) >= rank(summary.verdict)) summary.verdict = current;
    }
};

} // namespace ubsi::ui
