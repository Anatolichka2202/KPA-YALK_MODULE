#pragma once
#include "test_page.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QAbstractItemView>
#include <QTextStream>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <tuple>

namespace {

constexpr int kStandMode = 0;
constexpr int kDemoMode = 1;

enum class ScopeKind { Full = 0, Yalk = 1, Ytp = 2, Yvp = 3 };
enum class TopStage { Preparation = 0, Power = 1, Yalk = 2, Ytp = 3, Yvp = 4, Finish = 5 };
enum class YalkPhase { Init = 0, Calibration = 1, Initial = 2, Analog = 3, Discrete = 4, Overload = 5, Reference = 6 };
enum class YtpPhase { Init = 0, Calibration = 1, Channels = 2 };

QString verdictText(orbita::stand::RunVerdict verdict)
{
    using V = orbita::stand::RunVerdict;
    switch (verdict) {
    case V::Ok: return QStringLiteral("НОРМА");
    case V::Fail: return QStringLiteral("НЕ НОРМА");
    case V::Incomplete: return QStringLiteral("НЕПОЛНАЯ");
    case V::Aborted: return QStringLiteral("ОСТАНОВЛЕНО");
    case V::Error: return QStringLiteral("ОШИБКА");
    case V::NotRun: return QStringLiteral("НЕ ВЫПОЛНЯЛОСЬ");
    }
    return QStringLiteral("ОШИБКА");
}

QColor verdictColor(orbita::stand::RunVerdict verdict)
{
    using V = orbita::stand::RunVerdict;
    if (verdict == V::Ok) return QColor("#70d79b");
    if (verdict == V::Incomplete) return QColor("#69aee6");
    if (verdict == V::Aborted) return QColor("#d7a95b");
    return QColor("#e1766d");
}

QString eventValue(const orbita::stand::RunEvent& event, const char* key)
{
    const auto found = event.data.find(key);
    return found == event.data.end() ? QString() : QString::fromStdString(found->second);
}

QVector<double> csvNumbers(const QString& text)
{
    QVector<double> values;
    for (const auto& token : text.split(',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const double v = token.toDouble(&ok);
        if (ok && std::isfinite(v)) values.push_back(v);
    }
    return values;
}

QFrame* panel(bool accent = false)
{
    auto* frame = new QFrame;
    frame->setObjectName(accent ? QStringLiteral("accentPanel") : QStringLiteral("panel"));
    return frame;
}

QLabel* titleLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("pageTitle"));
    return label;
}

QLabel* subtitleLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("pageSubtitle"));
    label->setWordWrap(true);
    return label;
}

QLabel* sectionLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("sectionTitle"));
    return label;
}

QLabel* mutedLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("muted"));
    return label;
}

QFrame* metricCard(const QString& caption, QLabel*& value)
{
    auto* frame = panel();
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(2);
    layout->addWidget(mutedLabel(caption));
    value = new QLabel(QStringLiteral("—"));
    value->setObjectName(QStringLiteral("metricValue"));
    layout->addWidget(value);
    return frame;
}

QString elapsedText(qint64 milliseconds)
{
    const qint64 seconds = std::max<qint64>(0, milliseconds / 1000);
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QChar('0'))
        .arg((seconds / 60) % 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0'));
}

class TrendPlot final : public QWidget
{
public:
    explicit TrendPlot(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(105);
    }

    void configure(QString title, QString unit, QString firstName = {}, QString secondName = {})
    {
        title_ = std::move(title);
        unit_ = std::move(unit);
        firstName_ = std::move(firstName);
        secondName_ = std::move(secondName);
        update();
    }

    void clear()
    {
        first_.clear();
        second_.clear();
        update();
    }

    void append(double first, double second = std::numeric_limits<double>::quiet_NaN())
    {
        first_.push_back(first);
        if (std::isfinite(second)) second_.push_back(second);
        else if (!second_.isEmpty()) second_.push_back(std::numeric_limits<double>::quiet_NaN());
        while (first_.size() > 180) first_.removeFirst();
        while (second_.size() > 180) second_.removeFirst();
        update();
    }

    void setSamples(const QVector<double>& samples)
    {
        first_ = samples;
        second_.clear();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#0e1115"));
        QFont font = painter.font(); font.setBold(true); painter.setFont(font);
        painter.setPen(QColor("#dce6ef"));
        painter.drawText(QRectF(9, 4, width() - 18, 20), Qt::AlignLeft, title_);
        QVector<double> values;
        for (double v : first_) if (std::isfinite(v)) values.push_back(v);
        for (double v : second_) if (std::isfinite(v)) values.push_back(v);
        if (values.isEmpty()) {
            painter.setPen(QColor("#667484"));
            painter.drawText(QRectF(12, 30, width() - 24, height() - 40), Qt::AlignCenter,
                             QStringLiteral("ожидание измерения"));
            return;
        }
        const auto mm = std::minmax_element(values.begin(), values.end());
        const double latestFirst = first_.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : first_.last();
        const double latestSecond = second_.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : second_.last();
        const int decimals = unit_ == QStringLiteral("Ом") ? 2 : 3;
        const auto drawValue = [&](const QRectF& area, const QString& caption, double value,
                                   const QColor& color) {
            painter.setPen(QPen(QColor("#27313c"), 1));
            painter.setBrush(QColor("#111820"));
            painter.drawRoundedRect(area, 6, 6);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QColor("#7e8a98"));
            painter.setFont(QFont("Segoe UI", 8));
            painter.drawText(area.adjusted(10, 6, -10, -6), Qt::AlignTop | Qt::AlignLeft, caption);
            painter.setPen(color);
            painter.setFont(QFont("Segoe UI", 20, QFont::DemiBold));
            painter.drawText(area.adjusted(10, 22, -10, -6), Qt::AlignLeft | Qt::AlignVCenter,
                std::isfinite(value) ? QStringLiteral("%1 %2").arg(QString::number(value, 'f', decimals), unit_)
                                     : QStringLiteral("—"));
        };
        const double gap = 8.0;
        const double cardWidth = second_.isEmpty() ? (width() - 24.0) / 3.0
                                                    : (width() - 32.0) / 4.0;
        double left = 8.0;
        drawValue(QRectF(left, 30, cardWidth, height() - 38),
                  firstName_.isEmpty() ? QStringLiteral("Текущее") : firstName_, latestFirst,
                  QColor("#70d79b"));
        left += cardWidth + gap;
        if (!second_.isEmpty()) {
            drawValue(QRectF(left, 30, cardWidth, height() - 38),
                      secondName_.isEmpty() ? QStringLiteral("Измерено") : secondName_, latestSecond,
                      QColor("#69aee6"));
            left += cardWidth + gap;
        }
        drawValue(QRectF(left, 30, cardWidth, height() - 38), QStringLiteral("Минимум"), *mm.first,
                  QColor("#c2ccd8"));
        left += cardWidth + gap;
        drawValue(QRectF(left, 30, cardWidth, height() - 38), QStringLiteral("Максимум"), *mm.second,
                  QColor("#c2ccd8"));
    }

private:
    QString title_;
    QString unit_;
    QString firstName_;
    QString secondName_;
    QVector<double> first_;
    QVector<double> second_;
};

class StepPlot final : public QWidget
{
public:
    explicit StepPlot(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(105); }

    void configure(QString title, QString unit, QVector<double> steps)
    {
        title_ = std::move(title);
        unit_ = std::move(unit);
        steps_ = std::move(steps);
        update();
    }

    void setActiveValue(double value)
    {
        active_ = -1;
        double best = std::numeric_limits<double>::max();
        for (int i = 0; i < steps_.size(); ++i) {
            const double distance = std::abs(steps_[i] - value);
            if (distance < best) { best = distance; active_ = i; }
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#0e1115"));
        painter.setPen(QColor("#dce6ef"));
        QFont font = painter.font(); font.setBold(true); painter.setFont(font);
        painter.drawText(QRectF(9, 4, width() - 18, 20), Qt::AlignLeft, title_);
        if (steps_.isEmpty()) return;

        const QRectF strip(10, 31, width() - 20, height() - 42);
        const double stepWidth = strip.width() / std::max(1, static_cast<int>(steps_.size()));
        for (int i = 0; i < steps_.size(); ++i) {
            const QRectF step(strip.left() + i * stepWidth + 3, strip.top() + 3,
                              stepWidth - 6, strip.height() - 6);
            const bool active = i == active_;
            const bool done = active_ >= 0 && i < active_;
            painter.setPen(active ? QColor("#5e93b8") : QColor("#27313c"));
            painter.setBrush(active ? QColor("#132f49") : done ? QColor("#14251c") : QColor("#111820"));
            painter.drawRoundedRect(step, 6, 6);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(active ? QColor("#9ac7ff") : done ? QColor("#70d79b") : QColor("#8b95a3"));
            painter.setFont(QFont("Segoe UI", 8, QFont::DemiBold));
            painter.drawText(step.adjusted(4, 4, -4, -4), Qt::AlignTop | Qt::AlignHCenter,
                             QString::number(i + 1));
            painter.setFont(QFont("Segoe UI", 12, QFont::DemiBold));
            painter.drawText(step.adjusted(4, 16, -4, -4), Qt::AlignCenter,
                             QStringLiteral("%1 %2").arg(steps_[i], 0, 'f', 1).arg(unit_));
        }
    }

private:
    QString title_;
    QString unit_;
    QVector<double> steps_;
    int active_ = -1;
};

struct ChannelSample {
    QString key;
    QString point;
    double reference = 0.0;
    double measured = 0.0;
    bool signal = false;
    bool passed = true;
    QVector<double> samples;
};

class ChannelOverview final : public QWidget
{
public:
    explicit ChannelOverview(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("channelHistogram"));
        setMinimumHeight(290);
        setMouseTracking(true);
    }

    void configure(int count, QString unit)
    {
        count_ = count;
        unit_ = std::move(unit);
        clear();
    }

    void clear()
    {
        items_.clear();
        discreteStates_.clear();
        backgroundMean_.clear();
        backgroundMinimum_.clear();
        backgroundMaximum_.clear();
        currentKey_.clear();
        currentPoint_.clear();
        selectedKey_.clear();
        selectionPinned_ = false;
        setProperty("backgroundChannelCount", 0);
        setProperty("renderedChannelCount", 0);
        update();
    }

    void setBackground(QVector<double> mean, QVector<double> minimum, QVector<double> maximum)
    {
        backgroundMean_ = std::move(mean);
        backgroundMinimum_ = std::move(minimum);
        backgroundMaximum_ = std::move(maximum);
        int available = 0;
        for (const auto& key : channelKeys()) {
            const int index = key.toInt() - 1;
            if (index >= 0 && index < backgroundMean_.size()
                && index < backgroundMinimum_.size() && index < backgroundMaximum_.size())
                ++available;
        }
        setProperty("backgroundChannelCount", available);
        setProperty("renderedChannelCount", available);
        update();
    }

    void add(ChannelSample sample)
    {
        bool replaced = false;
        for (auto& item : items_) {
            if (item.key == sample.key && item.point == sample.point) {
                item = sample;
                replaced = true;
                break;
            }
        }
        if (!replaced) items_.push_back(sample);
        currentKey_ = sample.key;
        currentPoint_ = sample.point;
        discreteStates_.insert(sample.key, sample.signal ? 1 : 0);
        if (!selectionPinned_) selectedKey_ = currentKey_;
        setProperty("renderedChannelCount", displayChannels().size());
        update();
    }

    void setDiscrete(const QString& key, bool signal)
    {
        discreteStates_.insert(key, signal ? 1 : 0);
        currentKey_ = key;
        if (!selectionPinned_) selectedKey_ = key;
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        for (const auto& hit : hits_) {
            if (hit.first.contains(event->position())) {
                selectedKey_ = hit.second;
                selectionPinned_ = true;
                update();
                break;
            }
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        for (const auto& hit : hits_) {
            if (!hit.first.contains(event->position())) continue;
            for (const auto& sample : displayChannels()) {
                if (sample.key != hit.second || !sample.hasData) continue;
                const int precision = unit_ == QStringLiteral("Ом") ? 2 : 3;
                const QString discrete = unit_ == QStringLiteral("Ом")
                    ? QString() : QStringLiteral("\nДискретный: %1")
                        .arg(sample.signalKnown ? QString::number(sample.signal) : QStringLiteral("не измерен"));
                QToolTip::showText(event->globalPosition().toPoint(),
                    QStringLiteral("Канал %1\nТекущее: %2 %3\nmin…max: %4…%5 %3\nРазмах: %6 %3%7")
                        .arg(sample.key, QString::number(sample.value, 'f', precision), unit_,
                             QString::number(sample.minimum, 'f', precision),
                             QString::number(sample.maximum, 'f', precision),
                             QString::number(sample.maximum - sample.minimum, 'f', precision), discrete),
                    this, hit.first.toRect());
                return;
            }
        }
        QToolTip::hideText();
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#0e1115"));
        hits_.clear();
        painter.setPen(QColor("#dce6ef"));
        QFont font = painter.font(); font.setBold(true); painter.setFont(font);
        painter.drawText(QRectF(10, 4, width() - 20, 20), Qt::AlignLeft,
            QStringLiteral("Все каналы · %1 · столбец: текущее · риска: min…max")
                .arg(currentPoint_.isEmpty() ? QStringLiteral("ожидание") : currentPoint_));

        const auto samples = displayChannels();
        if (selectedKey_.isEmpty() && !samples.isEmpty()) selectedKey_ = samples.first().key;
        const DisplayChannel* selected = nullptr;
        for (const auto& sample : samples)
            if (sample.key == selectedKey_ && sample.hasData) selected = &sample;
        if (!selected)
            for (const auto& sample : samples)
                if (sample.hasData) { selected = &sample; break; }

        if (selected) {
            painter.setFont(QFont("Segoe UI", 9, QFont::DemiBold));
            painter.setPen(QColor("#9ac7ff"));
            const QString discrete = unit_ == QStringLiteral("Ом")
                ? QString() : QStringLiteral("   D=%1")
                    .arg(selected->signalKnown ? QString::number(selected->signal) : QStringLiteral("?"));
            painter.drawText(QRectF(width() - 570, 4, 560, 20), Qt::AlignRight,
                QStringLiteral("Канал %1   %2 %3   min…max %4…%5   Δ %6%7")
                    .arg(selected->key, QString::number(selected->value, 'f', unit_ == QStringLiteral("Ом") ? 2 : 3), unit_,
                         QString::number(selected->minimum, 'f', unit_ == QStringLiteral("Ом") ? 2 : 3),
                         QString::number(selected->maximum, 'f', unit_ == QStringLiteral("Ом") ? 2 : 3),
                         QString::number(selected->maximum - selected->minimum, 'f', unit_ == QStringLiteral("Ом") ? 2 : 3),
                         discrete));
        }

        const double footerHeight = unit_ == QStringLiteral("Ом") ? 72.0 : 88.0;
        const QRectF area(58, 31, width() - 70, height() - 31 - footerHeight);
        painter.setPen(QPen(QColor("#27313c"), 1));
        painter.drawRect(area);
        const int dataCount = std::count_if(samples.cbegin(), samples.cend(),
            [](const DisplayChannel& sample) { return sample.hasData; });
        setProperty("renderedChannelCount", dataCount);
        if (dataCount == 0) {
            painter.setPen(QColor("#667484"));
            painter.drawText(area, Qt::AlignCenter, QStringLiteral("поканальные данные появятся во время проверки"));
            return;
        }

        double observedMaximum = 0.0;
        for (const auto& sample : samples) {
            if (!sample.hasData) continue;
            observedMaximum = std::max({observedMaximum, sample.value, sample.maximum});
        }
        const double physicalMaximum = unit_ == QStringLiteral("Ом") ? 240.0 : 6.2;
        const double lower = 0.0;
        const double upper = std::max(physicalMaximum, observedMaximum * 1.05);
        const auto y = [&](double value) {
            return area.bottom() - std::clamp((value - lower) / (upper - lower), 0.0, 1.0) * area.height();
        };
        painter.setFont(QFont("Segoe UI", 8));
        for (int tick = 0; tick <= 4; ++tick) {
            const double value = lower + (upper - lower) * tick / 4.0;
            const double position = y(value);
            painter.setPen(QPen(QColor("#27313c"), 1, Qt::DashLine));
            painter.drawLine(QPointF(area.left(), position), QPointF(area.right(), position));
            painter.setPen(QColor("#8b95a3"));
            painter.drawText(QRectF(0, position - 8, 52, 16), Qt::AlignRight,
                             QString::number(value, 'f', unit_ == QStringLiteral("Ом") ? 2 : 3));
        }

        const double cellWidth = area.width() / std::max(1, static_cast<int>(samples.size()));
        for (int index = 0; index < samples.size(); ++index) {
            const auto& sample = samples[index];
            const double x = area.left() + index * cellWidth;
            hits_.push_back({QRectF(x, area.top(), cellWidth, area.height() + 34), sample.key});
            if (sample.key == selectedKey_)
                painter.fillRect(QRectF(x, area.top(), cellWidth, area.height()), QColor(94,147,184,28));
            if (sample.hasData) {
                const double measuredY = y(sample.value);
                painter.setPen(Qt::NoPen);
                painter.setBrush(sample.measured
                    ? (sample.passed ? QColor("#4f9f78") : QColor("#cf5d62"))
                    : QColor("#3e7da1"));
                painter.drawRect(QRectF(x + cellWidth * 0.18, measuredY,
                                        std::max(2.0, cellWidth * 0.64), area.bottom() - measuredY));
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor("#e6edf3"), sample.key == selectedKey_ ? 2.0 : 1.0));
                painter.drawLine(QPointF(x + cellWidth / 2.0, y(sample.minimum)),
                                 QPointF(x + cellWidth / 2.0, y(sample.maximum)));
                painter.drawLine(QPointF(x + cellWidth * 0.28, y(sample.minimum)),
                                 QPointF(x + cellWidth * 0.72, y(sample.minimum)));
                painter.drawLine(QPointF(x + cellWidth * 0.28, y(sample.maximum)),
                                 QPointF(x + cellWidth * 0.72, y(sample.maximum)));
            }
            painter.setPen(QColor("#9aafbf"));
            painter.setFont(QFont("Segoe UI", count_ > 40 ? 7 : 8));
            painter.drawText(QRectF(x, area.bottom() + 3, cellWidth, 14),
                             Qt::AlignCenter, sample.key);
            if (unit_ != QStringLiteral("Ом")) {
                painter.setPen(sample.signalKnown && sample.signal ? QColor("#70d79b") : QColor("#8b95a3"));
                painter.drawText(QRectF(x, area.bottom() + 18, cellWidth, 13), Qt::AlignCenter,
                                 sample.signalKnown ? QString::number(sample.signal) : QStringLiteral("?"));
            }
        }

        QVector<double> spans;
        spans.reserve(samples.size());
        double maximumSpan = 0.0;
        for (const auto& sample : samples) {
            spans.push_back(sample.hasData ? std::max(0.0, sample.maximum - sample.minimum) : 0.0);
            maximumSpan = std::max(maximumSpan, spans.last());
        }
        const QRectF spanArea(area.left(), area.bottom() + 34, area.width(), 20);
        painter.setPen(QColor("#8b95a3"));
        painter.setFont(QFont("Segoe UI", 8));
        painter.drawText(QRectF(0, spanArea.top(), 52, 18), Qt::AlignRight,
                         QStringLiteral("Δ %1").arg(unit_));
        painter.setPen(QPen(QColor("#27313c"), 1));
        painter.drawLine(spanArea.bottomLeft(), spanArea.bottomRight());
        const double spanScale = std::max(maximumSpan, unit_ == QStringLiteral("Ом") ? 0.01 : 0.0001);
        for (int index = 0; index < spans.size(); ++index) {
            const double x = spanArea.left() + index * cellWidth;
            const double height = spans[index] / spanScale * spanArea.height();
            painter.fillRect(QRectF(x + cellWidth * 0.2, spanArea.bottom() - height,
                                    std::max(2.0, cellWidth * 0.6), height), QColor("#62a6d8"));
        }
        painter.setPen(QColor("#9ac7ff"));
        painter.drawText(QRectF(spanArea.left(), spanArea.top(), spanArea.width(), 16),
                         Qt::AlignRight | Qt::AlignTop,
                         QStringLiteral("шкала размаха 0…%1 %2")
                             .arg(maximumSpan, 0, 'f', unit_ == QStringLiteral("Ом") ? 2 : 4)
                             .arg(unit_));
    }

private:
    struct DisplayChannel {
        QString key;
        double value = 0.0;
        double minimum = 0.0;
        double maximum = 0.0;
        bool hasData = false;
        bool measured = false;
        bool passed = true;
        bool signalKnown = false;
        int signal = 0;
    };

    QVector<QString> channelKeys() const
    {
        QVector<QString> keys;
        if (count_ == 80 && unit_ == QStringLiteral("В")) {
            for (int address = 1; address <= 87; ++address)
                if (address <= 28 || (address >= 32 && address <= 43)
                    || (address >= 45 && address <= 70) || address >= 74)
                    keys.push_back(QString::number(address));
            return keys;
        }
        for (int channel = 1; channel <= count_; ++channel)
            keys.push_back(QString::number(channel));
        return keys;
    }

    QVector<DisplayChannel> displayChannels() const
    {
        QVector<DisplayChannel> channels;
        for (const auto& key : channelKeys()) {
            DisplayChannel channel;
            channel.key = key;
            const int backgroundIndex = key.toInt() - 1;
            if (backgroundIndex >= 0 && backgroundIndex < backgroundMean_.size()
                && backgroundIndex < backgroundMinimum_.size()
                && backgroundIndex < backgroundMaximum_.size()) {
                channel.value = backgroundMean_[backgroundIndex];
                channel.minimum = backgroundMinimum_[backgroundIndex];
                channel.maximum = backgroundMaximum_[backgroundIndex];
                channel.hasData = true;
            }
            const bool hasBackground = channel.hasData;
            for (auto item = items_.crbegin(); item != items_.crend(); ++item) {
                if (item->key != key || (!currentPoint_.isEmpty() && item->point != currentPoint_)) continue;
                channel.value = item->measured;
                channel.measured = true;
                channel.passed = item->passed;
                channel.hasData = true;
                if (!hasBackground) {
                    channel.minimum = item->measured;
                    channel.maximum = item->measured;
                    if (!item->samples.isEmpty()) {
                        const auto range = std::minmax_element(item->samples.cbegin(), item->samples.cend());
                        channel.minimum = *range.first;
                        channel.maximum = *range.second;
                    }
                }
                break;
            }
            const auto signal = discreteStates_.constFind(key);
            if (signal != discreteStates_.cend()) {
                channel.signalKnown = true;
                channel.signal = *signal;
            }
            channels.push_back(channel);
        }
        return channels;
    }

    int count_ = 80;
    QString unit_ = QStringLiteral("В");
    QVector<ChannelSample> items_;
    QString currentKey_;
    QString currentPoint_;
    QString selectedKey_;
    QVector<QPair<QRectF, QString>> hits_;
    QVector<double> backgroundMean_;
    QVector<double> backgroundMinimum_;
    QVector<double> backgroundMaximum_;
    QHash<QString, int> discreteStates_;
    bool selectionPinned_ = false;
};

class StateGrid final : public QWidget
{
public:
    explicit StateGrid(QWidget* parent=nullptr):QWidget(parent){setMinimumHeight(280);}
    void clear(){states_.clear();current_.clear();expected_=0;update();}
    void setCurrent(QString key,int signal,int expected){states_[key]=signal;current_=std::move(key);expected_=expected;update();}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#0e1115"));
        const int cols=10;const double gap=3,m=8;const int rows=8;
        const double cw=(width()-2*m-gap*(cols-1))/cols;const double ch=(height()-2*m-gap*(rows-1))/rows;
        for(int i=1;i<=80;++i){const int r=(i-1)/cols,c=(i-1)%cols;QRectF cell(m+c*(cw+gap),m+r*(ch+gap),cw,ch);
            const QString key=QString::number(i);const bool known=states_.contains(key);const int state=states_.value(key,0);
            QColor fill=known?(state==expected_?QColor("#111820"):QColor("#261719")):QColor("#111820");
            QColor border=known?(state==expected_?QColor("#344557"):QColor("#8f4549")):QColor("#27313c");
            if(key==current_){fill=QColor("#132033");border=QColor("#5e93b8");}
            p.setPen(QPen(border,key==current_?2:1));p.setBrush(fill);p.drawRoundedRect(cell,3,3);
            p.setBrush(Qt::NoBrush);p.setPen(QColor("#9aafbf"));p.setFont(QFont("Segoe UI",8));
            p.drawText(cell.adjusted(5,2,-5,-2),Qt::AlignTop|Qt::AlignLeft,QStringLiteral("Канал %1").arg(key));
            p.setPen(known?(state==expected_?QColor("#dce6ef"):QColor("#e1766d")):QColor("#667484"));
            p.setFont(QFont("Segoe UI",11,QFont::DemiBold));
            p.drawText(cell.adjusted(5,12,-5,-2),Qt::AlignBottom|Qt::AlignLeft,
                       known?QStringLiteral("D = %1").arg(state):QStringLiteral("D = —"));
        }
    }
private:QHash<QString,int> states_;QString current_;int expected_=0;
};

QString pageStyle()
{
    return QStringLiteral(R"QSS(
QWidget#operatorTestPage { background:#14171c; color:#e6eaf0; }
QFrame#panel { background:#10151b; border:1px solid #27313c; border-radius:8px; }
QFrame#accentPanel { background:#132033; border:1px solid #27466c; border-radius:8px; }
QLabel#pageTitle { color:#f1f5f9; font-size:27px; font-weight:700; }
QLabel#pageSubtitle { color:#8b95a3; font-size:13px; }
QLabel#sectionTitle { color:#e6eaf0; font-size:15px; font-weight:700; }
QLabel#muted { color:#7e8a98; font-size:11px; }
QLabel#metricValue { color:#f1f5f9; font-size:20px; font-weight:700; }
QPushButton { background:#1b2129; color:#c2ccd8; border:1px solid #2c333d; padding:7px 13px; border-radius:6px; }
QPushButton:hover { background:#2a313b; border-color:#5e93b8; }
QPushButton:disabled { color:#5b6573; background:#1c2128; border-color:#232a33; }
QPushButton#primary { background:#214e78; border:1px solid #5e93b8; color:#eef7ff; font-weight:700; padding:10px 18px; }
QPushButton#scopeCard, QPushButton#subCheckCard { background:#12161c; color:#dfe6ee; border:1px solid #2c333d; border-radius:9px; text-align:left; padding:13px; font-weight:650; }
QPushButton#scopeCard { min-height:74px; font-size:14px; }
QPushButton#subCheckCard { min-height:64px; font-size:12px; }
QPushButton#scopeCard:hover, QPushButton#subCheckCard:hover { background:#1b2129; border:2px solid #5e93b8; }
QPushButton#scopeCard:checked, QPushButton#subCheckCard:checked { background:#18212c; border:2px solid #5e93b8; color:#9ac7ff; }
QLineEdit, QComboBox { background:#1b2129; color:#e6eaf0; border:1px solid #2c333d; border-radius:5px; padding:7px 9px; }
QLineEdit:focus, QComboBox:focus { border-color:#5e93b8; }
QTableWidget { background:#0e1115; alternate-background-color:#1c2128; border:1px solid #232a33; gridline-color:#1a1f26; }
QHeaderView::section { background:#0e1115; color:#7e8a98; padding:7px; border:none; border-right:1px solid #232a33; font-weight:600; }
QProgressBar { background:#1c222a; border:1px solid #232a33; border-radius:4px; min-height:18px; text-align:center; }
QProgressBar::chunk { background:#5e93b8; border-radius:3px; }
QLabel#stage { color:#8d9aa8; padding:7px 9px; border:1px solid transparent; border-radius:5px; }
QLabel#stageActive { background:#132033; border:1px solid #27466c; border-left:4px solid #5e93b8; color:#9ac7ff; padding:7px 9px; font-weight:700; border-radius:5px; }
QLabel#stageDone { color:#70d79b; padding:7px 9px; border:1px solid transparent; }
QToolTip { background:#1b2129; border:1px solid #3a424d; color:#e6eaf0; padding:7px 10px; }
)QSS");
}

} // namespace
