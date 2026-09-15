#pragma once

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QVector>
#include <QHash>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// Data-dense operator widgets. Presentation consumes backend values and
// verdicts; no technological acceptance rule is invented here.

class OverloadOverview final : public QWidget
{
public:
    explicit OverloadOverview(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("yalkOverloadOverview"));
        setMinimumHeight(410);
        setMouseTracking(true);
        baseline_.fill(0.0, 88);
        current_.fill(0.0, 88);
        delta_.fill(0.0, 88);
        baselineKnown_.fill(false, 88);
        currentKnown_.fill(false, 88);
        passed_.fill(true, 88);
    }

    void clear()
    {
        baseline_.fill(0.0);
        current_.fill(0.0);
        delta_.fill(0.0);
        baselineKnown_.fill(false);
        currentKnown_.fill(false);
        passed_.fill(true);
        stressed_ = impactIndex_ = impactCount_ = settleMs_ = 0;
        polarity_.clear();
        lower_ = -2.0;
        upper_ = 2.0;
        hits_.clear();
        setProperty("overloadMeasurementCount", 0);
        update();
    }

    void beginImpact(int stressed, QString polarity, int impactIndex, int impactCount, int settleMs)
    {
        current_.fill(0.0);
        delta_.fill(0.0);
        currentKnown_.fill(false);
        passed_.fill(true);
        stressed_ = stressed;
        polarity_ = std::move(polarity);
        impactIndex_ = impactIndex;
        impactCount_ = impactCount;
        settleMs_ = settleMs;
        hits_.clear();
        setProperty("overloadMeasurementCount", 0);
        update();
    }

    void setMeasurement(int observed, double baseline, double current, double delta,
                        double lower, double upper, bool passed)
    {
        if (observed < 1 || observed > 88) return;
        const int index = observed - 1;
        baseline_[index] = baseline;
        current_[index] = current;
        delta_[index] = delta;
        baselineKnown_[index] = true;
        currentKnown_[index] = true;
        passed_[index] = passed;
        lower_ = lower;
        upper_ = upper;
        setProperty("overloadMeasurementCount",
                    std::count(currentKnown_.cbegin(), currentKnown_.cend(), true));
        update();
    }

    double maximumAbsoluteDelta() const
    {
        double maximum = 0.0;
        for (int index = 0; index < delta_.size(); ++index)
            if (currentKnown_[index]) maximum = std::max(maximum, std::abs(delta_[index]));
        return maximum;
    }

protected:
    void mouseMoveEvent(QMouseEvent* event) override
    {
        for (const auto& hit : hits_) {
            if (!hit.first.contains(event->position())) continue;
            const int index = hit.second;
            if (index < 0 || index >= 88 || !currentKnown_[index]) break;
            QToolTip::showText(event->globalPosition().toPoint(),
                QStringLiteral("Канал %1\nbaseline: %2\ncurrent: %3\nΔcode: %4\n%5")
                    .arg(index + 1)
                    .arg(baseline_[index], 0, 'f', 1)
                    .arg(current_[index], 0, 'f', 1)
                    .arg(delta_[index], 0, 'f', 1)
                    .arg(passed_[index] ? QStringLiteral("НОРМА") : QStringLiteral("НЕ НОРМА")),
                this, hit.first.toRect());
            return;
        }
        QToolTip::hideText();
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#0f1318"));
        hits_.clear();

        painter.setPen(QColor("#dfe6ee"));
        painter.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
        painter.drawText(QRectF(58, 5, width() - 70, 22), Qt::AlignLeft,
                         QStringLiteral("88 каналов · baseline + current"));
        painter.setPen(QColor("#8da4b8"));
        painter.setFont(QFont("Segoe UI", 8));
        painter.drawText(QRectF(58, 5, width() - 70, 22), Qt::AlignRight,
            QStringLiteral("%1 · канал %2 · воздействие %3/%4 · выдержка %5 с")
                .arg(polarity_.isEmpty() ? QStringLiteral("±12 В") : polarity_)
                .arg(stressed_ > 0 ? QString::number(stressed_) : QStringLiteral("—"))
                .arg(impactIndex_).arg(impactCount_).arg(settleMs_ / 1000.0, 0, 'f', 1));

        painter.setFont(QFont("Segoe UI", 8));
        const double legendY = 31.0;
        painter.fillRect(QRectF(58, legendY + 3, 12, 8), QColor(90, 117, 139, 85));
        painter.setPen(QColor("#8b95a3"));
        painter.drawText(QRectF(75, legendY, 105, 16), Qt::AlignVCenter, QStringLiteral("baseline"));
        painter.fillRect(QRectF(164, legendY + 3, 12, 8), QColor("#58bdd3"));
        painter.drawText(QRectF(181, legendY, 105, 16), Qt::AlignVCenter, QStringLiteral("current"));
        painter.fillRect(QRectF(270, legendY + 3, 12, 8), QColor("#386f9d"));
        painter.drawText(QRectF(287, legendY, 155, 16), Qt::AlignVCenter, QStringLiteral("канал воздействия"));
        painter.fillRect(QRectF(430, legendY + 3, 12, 8), QColor("#cf5d62"));
        painter.drawText(QRectF(447, legendY, 170, 16), Qt::AlignVCenter, QStringLiteral("нарушение |Δcode| ≤ 2"));

        const QRectF area(58, 55, width() - 70, height() - 106);
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
        for (int index = 0; index < 88; ++index) {
            if (baselineKnown_[index]) {
                minimum = std::min(minimum, baseline_[index]);
                maximum = std::max(maximum, baseline_[index]);
            }
            if (currentKnown_[index]) {
                minimum = std::min(minimum, current_[index]);
                maximum = std::max(maximum, current_[index]);
            }
        }
        if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
            painter.setPen(QPen(QColor("#27313c"), 1));
            painter.drawRect(area);
            painter.setPen(QColor("#667484"));
            painter.drawText(area, Qt::AlignCenter,
                             QStringLiteral("ожидание baseline / current"));
            return;
        }
        double span = maximum - minimum;
        const double padding = std::max(1.0, span * 0.16);
        minimum -= padding;
        maximum += padding;
        if (!(maximum > minimum)) maximum = minimum + 1.0;
        const auto y = [&](double value) {
            return area.bottom() - (value - minimum) / (maximum - minimum) * area.height();
        };

        painter.setPen(QPen(QColor("#27313c"), 1));
        painter.drawRect(area);
        painter.setFont(QFont("Segoe UI", 7));
        for (int tick = 0; tick <= 4; ++tick) {
            const double value = minimum + (maximum - minimum) * tick / 4.0;
            const double yy = y(value);
            painter.setPen(QPen(QColor("#27313c"), 1, Qt::DashLine));
            painter.drawLine(QPointF(area.left(), yy), QPointF(area.right(), yy));
            painter.setPen(QColor("#8293a4"));
            painter.drawText(QRectF(0, yy - 8, 52, 16), Qt::AlignRight,
                             QString::number(value, 'f', 1));
        }

        const double cellWidth = area.width() / 88.0;
        for (int index = 0; index < 88; ++index) {
            const double x = area.left() + index * cellWidth;
            hits_.push_back({QRectF(x, area.top(), cellWidth, area.height()), index});
            if (index + 1 == stressed_) {
                painter.fillRect(QRectF(x, area.top(), cellWidth, area.height()),
                                 QColor(56, 111, 157, 42));
                painter.setPen(QPen(QColor("#69aee6"), 1.5));
                painter.drawLine(QPointF(x + cellWidth / 2.0, area.top()),
                                 QPointF(x + cellWidth / 2.0, area.bottom()));
            }
            if (baselineKnown_[index]) {
                const double top = y(baseline_[index]);
                painter.fillRect(QRectF(x + cellWidth * 0.10, top,
                                        std::max(2.0, cellWidth * 0.80), area.bottom() - top),
                                 QColor(90, 117, 139, 85));
            }
            if (currentKnown_[index]) {
                const double top = y(current_[index]);
                const QColor color = passed_[index] ? QColor("#58bdd3") : QColor("#cf5d62");
                painter.fillRect(QRectF(x + cellWidth * 0.27, top,
                                        std::max(2.0, cellWidth * 0.46), area.bottom() - top), color);
            }
            if (index == 0 || index == 87 || (index + 1) % 5 == 0) {
                painter.setPen(QColor("#9aafbf"));
                painter.setFont(QFont("Segoe UI", 7));
                painter.drawText(QRectF(x - cellWidth, area.bottom() + 4, cellWidth * 3, 15),
                                 Qt::AlignCenter, QString::number(index + 1));
            }
        }

        painter.setPen(QColor("#8b95a3"));
        painter.setFont(QFont("Segoe UI", 8));
        painter.drawText(QRectF(area.left(), height() - 29, area.width(), 18), Qt::AlignLeft,
            QStringLiteral("критерий |current − baseline| ≤ %1 кода · получено %2 из %3 наблюдаемых каналов")
                .arg(std::max(std::abs(lower_), std::abs(upper_)), 0, 'f', 0)
                .arg(property("overloadMeasurementCount").toInt())
                .arg(stressed_ > 0 ? 87 : 88));
        painter.setPen(maximumAbsoluteDelta() > std::max(std::abs(lower_), std::abs(upper_))
                           ? QColor("#cf5d62") : QColor("#9ac7ff"));
        painter.drawText(QRectF(area.left(), height() - 29, area.width(), 18), Qt::AlignRight,
                         QStringLiteral("max |Δcode| = %1")
                             .arg(maximumAbsoluteDelta(), 0, 'f', 1));
    }

private:
    QVector<double> baseline_, current_, delta_;
    QVector<bool> baselineKnown_, currentKnown_, passed_;
    QVector<QPair<QRectF, int>> hits_;
    int stressed_ = 0;
    int impactIndex_ = 0;
    int impactCount_ = 0;
    int settleMs_ = 0;
    QString polarity_;
    double lower_ = -2.0;
    double upper_ = 2.0;
};

class YalkAnalogOverview final : public QWidget
{
public:
    explicit YalkAnalogOverview(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("yalkAnalogOverviewV05"));
        setMinimumHeight(360);
        setMouseTracking(true);
        channels_.fill(Channel{});
    }

    void clear()
    {
        channels_.fill(Channel{});
        currentAddress_ = 0;
        pinnedAddress_ = 0;
        currentCommand_ = 0.0;
        currentReference_ = 0.0;
        lowerLimit_ = upperLimit_ = std::numeric_limits<double>::quiet_NaN();
        fullScale_ = false;
        hits_.clear();
        setProperty("renderedChannelCount", 0);
        update();
    }

    void setBackground(const QVector<double>& mean,
                       const QVector<double>& minimum,
                       const QVector<double>& maximum)
    {
        const int count = std::min({80, mean.size(), minimum.size(), maximum.size()});
        for (int index = 0; index < count; ++index) {
            channels_[index].value = mean[index];
            channels_[index].minimum = minimum[index];
            channels_[index].maximum = maximum[index];
            channels_[index].known = true;
            channels_[index].backgroundOnly = true;
        }
        updateCount();
        update();
    }

    void setMeasurement(int address, double command, double reference, double measured,
                        double minimum, double maximum, double lowerLimit, double upperLimit,
                        bool passed, bool warning)
    {
        const int index = channelKeys().indexOf(address);
        if (index < 0) return;
        auto& channel = channels_[index];
        channel.value = measured;
        channel.minimum = minimum;
        channel.maximum = maximum;
        channel.known = true;
        channel.backgroundOnly = false;
        channel.passed = passed;
        channel.warning = warning;
        currentAddress_ = address;
        currentCommand_ = command;
        currentReference_ = reference;
        lowerLimit_ = lowerLimit;
        upperLimit_ = upperLimit;
        if (pinnedAddress_ == 0) pinnedAddress_ = address;
        updateCount();
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (fullScaleHit_.contains(event->position())) {
            fullScale_ = !fullScale_;
            update();
            return;
        }
        for (const auto& hit : hits_) {
            if (!hit.first.contains(event->position())) continue;
            pinnedAddress_ = hit.second;
            update();
            return;
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        for (const auto& hit : hits_) {
            if (!hit.first.contains(event->position())) continue;
            const int index = channelKeys().indexOf(hit.second);
            if (index < 0 || !channels_[index].known) break;
            const auto& channel = channels_[index];
            QToolTip::showText(event->globalPosition().toPoint(),
                QStringLiteral("Канал %1\nтекущее: %2 В\nmin…max: %3…%4 В\nразмах: %5 В")
                    .arg(hit.second)
                    .arg(channel.value, 0, 'f', 4)
                    .arg(channel.minimum, 0, 'f', 4)
                    .arg(channel.maximum, 0, 'f', 4)
                    .arg(channel.maximum - channel.minimum, 0, 'f', 4),
                this, hit.first.toRect());
            return;
        }
        QToolTip::hideText();
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#0f1318"));
        hits_.clear();

        painter.setPen(QColor("#dfe6ee"));
        painter.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
        painter.drawText(QRectF(58, 5, width() - 360, 22), Qt::AlignLeft,
            QStringLiteral("Все 80 каналов · рабочая шкала точки %1 В")
                .arg(currentCommand_, 0, 'f', 1));

        fullScaleHit_ = QRectF(width() - 245, 4, 230, 24);
        painter.setPen(QPen(fullScale_ ? QColor("#69aee6") : QColor("#344454"), 1));
        painter.setBrush(fullScale_ ? QColor("#16314a") : QColor("#111820"));
        painter.drawRoundedRect(fullScaleHit_, 5, 5);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(fullScale_ ? QColor("#9ac7ff") : QColor("#9aafbf"));
        painter.setFont(QFont("Segoe UI", 8, QFont::DemiBold));
        painter.drawText(fullScaleHit_, Qt::AlignCenter,
            fullScale_ ? QStringLiteral("Полная шкала 0…6,2 В")
                       : QStringLiteral("Показать полную шкалу 0…6,2 В"));

        double lower = 0.0;
        double upper = 0.0;
        if (fullScale_) {
            lower = -0.08;
            upper = 6.32;
        } else if (std::abs(currentCommand_) < 0.1) {
            lower = -0.12;
            upper = 0.16;
        } else {
            lower = currentCommand_ - 0.15;
            upper = currentCommand_ + 0.16;
        }

        if (!fullScale_) {
            double dataMinimum = std::min(currentReference_, currentCommand_);
            double dataMaximum = std::max(currentReference_, currentCommand_);
            for (const auto& channel : channels_) {
                if (!channel.known) continue;
                dataMinimum = std::min(dataMinimum, channel.minimum);
                dataMaximum = std::max(dataMaximum, channel.maximum);
            }
            if (std::isfinite(lowerLimit_)) dataMinimum = std::min(dataMinimum, lowerLimit_);
            if (std::isfinite(upperLimit_)) dataMaximum = std::max(dataMaximum, upperLimit_);
            lower = std::min(lower, dataMinimum - 0.02);
            upper = std::max(upper, dataMaximum + 0.02);
        }
        if (!(upper > lower)) upper = lower + 0.1;

        const QRectF area(58, 48, width() - 70, height() - 112);
        const auto y = [&](double value) {
            return area.bottom() - std::clamp((value - lower) / (upper - lower), 0.0, 1.0) * area.height();
        };

        painter.setPen(QPen(QColor("#27313c"), 1));
        painter.drawRect(area);
        painter.setFont(QFont("Segoe UI", 7));
        for (int tick = 0; tick <= 4; ++tick) {
            const double value = lower + (upper - lower) * tick / 4.0;
            const double yy = y(value);
            painter.setPen(QPen(QColor("#27313c"), 1, Qt::DashLine));
            painter.drawLine(QPointF(area.left(), yy), QPointF(area.right(), yy));
            painter.setPen(QColor("#8293a4"));
            painter.drawText(QRectF(0, yy - 8, 52, 16), Qt::AlignRight,
                             QString::number(value, 'f', 3));
        }

        if (std::isfinite(lowerLimit_) && std::isfinite(upperLimit_)) {
            const double top = y(upperLimit_);
            const double bottom = y(lowerLimit_);
            painter.fillRect(QRectF(area.left(), top, area.width(), std::max(1.0, bottom - top)),
                             QColor(64, 126, 88, 24));
        }
        painter.setPen(QPen(QColor("#78c7ff"), 1.3, Qt::DashLine));
        painter.drawLine(QPointF(area.left(), y(currentReference_)),
                         QPointF(area.right(), y(currentReference_)));

        const auto keys = channelKeys();
        const double slotWidth = area.width() / 80.0;
        int visibleCount = 0;
        for (int index = 0; index < 80; ++index) {
            const int address = keys[index];
            const double x = area.left() + index * slotWidth;
            hits_.push_back({QRectF(x, area.top(), slotWidth, area.height() + 28), address});
            const auto& channel = channels_[index];
            if (address == currentAddress_) {
                painter.fillRect(QRectF(x, area.top(), slotWidth, area.height()), QColor(56, 111, 157, 38));
                painter.setPen(QPen(QColor("#69aee6"), 1.5));
                painter.drawLine(QPointF(x + slotWidth / 2.0, area.top()),
                                 QPointF(x + slotWidth / 2.0, area.bottom()));
            } else if (address == pinnedAddress_) {
                painter.fillRect(QRectF(x, area.top(), slotWidth, area.height()), QColor(105, 174, 230, 20));
            }
            if (channel.known) {
                ++visibleCount;
                const QColor color = !channel.passed ? QColor("#cf5d62")
                    : channel.warning ? QColor("#c48a32")
                                      : channel.backgroundOnly ? QColor(90, 117, 139, 115)
                                                               : QColor("#58bdd3");
                const double valueY = y(channel.value);
                const double zeroY = y(std::clamp(0.0, lower, upper));
                painter.fillRect(QRectF(x + slotWidth * 0.20,
                                        std::min(valueY, zeroY),
                                        std::max(2.0, slotWidth * 0.60),
                                        std::max(1.0, std::abs(zeroY - valueY))), color);
                painter.setPen(QPen(QColor("#e6edf3"), address == pinnedAddress_ ? 1.8 : 1.0));
                painter.drawLine(QPointF(x + slotWidth / 2.0, y(channel.minimum)),
                                 QPointF(x + slotWidth / 2.0, y(channel.maximum)));
            }
            if (index == 0 || index == 79 || address % 5 == 0) {
                painter.setPen(QColor("#9aafbf"));
                painter.setFont(QFont("Segoe UI", 7));
                painter.drawText(QRectF(x - slotWidth, area.bottom() + 3, slotWidth * 3, 14),
                                 Qt::AlignCenter, QString::number(address));
            }
        }
        setProperty("renderedChannelCount", visibleCount);

        painter.fillRect(QRectF(area.left(), height() - 38, 10, 8), QColor("#58bdd3"));
        painter.setPen(QColor("#8b95a3"));
        painter.setFont(QFont("Segoe UI", 8));
        painter.drawText(QRectF(area.left() + 15, height() - 43, 120, 18), Qt::AlignVCenter,
                         QStringLiteral("измерение канала"));
        painter.setPen(QColor("#78c7ff"));
        painter.drawText(QRectF(area.left() + 150, height() - 43, 210, 18), Qt::AlignVCenter,
                         QStringLiteral("— В7 %1 В").arg(currentReference_, 0, 'f', 3));
        painter.setPen(QColor("#9ac7ff"));
        const int inspect = pinnedAddress_ > 0 ? pinnedAddress_ : currentAddress_;
        const int inspectIndex = keys.indexOf(inspect);
        if (inspectIndex >= 0 && channels_[inspectIndex].known) {
            const auto& channel = channels_[inspectIndex];
            painter.drawText(QRectF(area.left(), height() - 43, area.width(), 18), Qt::AlignRight,
                QStringLiteral("канал %1 · %2 В · min…max %3…%4")
                    .arg(inspect)
                    .arg(channel.value, 0, 'f', 4)
                    .arg(channel.minimum, 0, 'f', 4)
                    .arg(channel.maximum, 0, 'f', 4));
        }
    }

private:
    struct Channel {
        double value = 0.0;
        double minimum = 0.0;
        double maximum = 0.0;
        bool known = false;
        bool backgroundOnly = false;
        bool passed = true;
        bool warning = false;
    };

    static QVector<int> channelKeys()
    {
        QVector<int> keys;
        keys.reserve(80);
        for (int value = 1; value <= 28; ++value) keys.push_back(value);
        for (int value = 32; value <= 43; ++value) keys.push_back(value);
        for (int value = 45; value <= 70; ++value) keys.push_back(value);
        for (int value = 74; value <= 87; ++value) keys.push_back(value);
        return keys;
    }

    void updateCount()
    {
        int count = 0;
        for (const auto& channel : channels_) if (channel.known) ++count;
        setProperty("renderedChannelCount", count);
    }

    std::array<Channel, 80> channels_{};
    QVector<QPair<QRectF, int>> hits_;
    QRectF fullScaleHit_;
    int currentAddress_ = 0;
    int pinnedAddress_ = 0;
    double currentCommand_ = 0.0;
    double currentReference_ = 0.0;
    double lowerLimit_ = std::numeric_limits<double>::quiet_NaN();
    double upperLimit_ = std::numeric_limits<double>::quiet_NaN();
    bool fullScale_ = false;
};

class YvpOverview final : public QWidget
{
public:
    explicit YvpOverview(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("yvpEightChannelOverview"));
        setMinimumHeight(430);
        setMouseTracking(true);
    }

    void clear()
    {
        for (auto& channel : data_) channel.clear();
        currentChannel_ = -1;
        currentKey_.clear();
        currentGain_ = currentFrequency_ = currentInputVpp_ = 0.0;
        acceptance_.clear();
        setProperty("yvpRenderedChannelCount", 0);
        setProperty("yvpCompletedPointCount", 0);
        update();
    }

    void setPoint(int channel, double referenceGain, double measuredGain,
                  double outputVrms, double frequency, double inputVpp,
                  const QString& acceptance)
    {
        if (channel < 1 || channel > 8) return;
        const QString key = pointKey(referenceGain, frequency);
        data_[channel - 1].insert(key, Point{referenceGain, measuredGain, outputVrms, true});
        currentChannel_ = channel - 1;
        currentKey_ = key;
        currentGain_ = referenceGain;
        currentFrequency_ = frequency;
        currentInputVpp_ = inputVpp;
        acceptance_ = acceptance;
        int currentCount = 0;
        int completed = 0;
        for (const auto& row : data_) {
            if (row.contains(currentKey_)) ++currentCount;
            completed += row.size();
        }
        setProperty("yvpRenderedChannelCount", currentCount);
        setProperty("yvpCompletedPointCount", completed);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#0f1318"));

        int completed = 0;
        for (const auto& row : data_) completed += row.size();
        setProperty("yvpCompletedPointCount", completed);

        painter.setPen(QColor("#dfe6ee"));
        painter.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
        painter.drawText(QRectF(12, 5, width() - 24, 22), Qt::AlignLeft,
                         QStringLiteral("ЯВП-8 · текущая точка + матрица 8 × 7 × 7"));
        painter.setPen(QColor("#8da4b8"));
        painter.setFont(QFont("Segoe UI", 8));
        painter.drawText(QRectF(12, 5, width() - 24, 22), Qt::AlignRight,
            QStringLiteral("K=%1 мВ/пКл · %2 Гц · Rigol %3 Vpp · %4/392")
                .arg(currentGain_, 0, 'g', 8)
                .arg(currentFrequency_, 0, 'g', 8)
                .arg(currentInputVpp_, 0, 'g', 8)
                .arg(completed));

        QVector<Point> points;
        points.reserve(8);
        for (const auto& channel : data_)
            points.push_back(channel.value(currentKey_, Point{}));

        double maximum = std::max(0.001, currentGain_);
        int knownCount = 0;
        for (const auto& point : points) {
            if (!point.known) continue;
            ++knownCount;
            maximum = std::max({maximum, point.reference, point.measured});
        }
        setProperty("yvpRenderedChannelCount", knownCount);

        const QRectF chart(48, 34, width() - 60, 165);
        painter.setPen(QPen(QColor("#27313c"), 1));
        painter.drawRect(chart);
        const double upper = maximum * 1.12;
        const auto y = [&](double value) {
            return chart.bottom() - std::clamp(value / upper, 0.0, 1.0) * chart.height();
        };
        painter.setFont(QFont("Segoe UI", 7));
        for (int tick = 0; tick <= 4; ++tick) {
            const double value = upper * tick / 4.0;
            const double yy = y(value);
            painter.setPen(QPen(QColor("#27313c"), 1, Qt::DashLine));
            painter.drawLine(QPointF(chart.left(), yy), QPointF(chart.right(), yy));
            painter.setPen(QColor("#8293a4"));
            painter.drawText(QRectF(0, yy - 8, 42, 16), Qt::AlignRight,
                             QString::number(value, 'g', 4));
        }

        const double slotWidth = chart.width() / 8.0;
        for (int index = 0; index < 8; ++index) {
            const double x = chart.left() + index * slotWidth;
            if (index == currentChannel_)
                painter.fillRect(QRectF(x, chart.top(), slotWidth, chart.height()), QColor(94, 147, 184, 32));
            const auto& point = points[index];
            if (point.known) {
                const double sentTop = y(point.reference);
                const double measuredTop = y(point.measured);
                painter.fillRect(QRectF(x + slotWidth * .18, sentTop,
                                        std::max(4.0, slotWidth * .25), chart.bottom() - sentTop),
                                 QColor("#5e93b8"));
                painter.fillRect(QRectF(x + slotWidth * .55, measuredTop,
                                        std::max(4.0, slotWidth * .25), chart.bottom() - measuredTop),
                                 QColor("#58bdd3"));
                painter.setPen(QColor("#8da4b8"));
                painter.setFont(QFont("Segoe UI", 7));
                painter.drawText(QRectF(x, chart.bottom() + 18, slotWidth, 14), Qt::AlignCenter,
                                 QStringLiteral("%1 Vrms").arg(point.outputVrms, 0, 'f', 4));
            }
            painter.setPen(index == currentChannel_ ? QColor("#9ac7ff") : QColor("#9aafbf"));
            painter.setFont(QFont("Segoe UI", 8, QFont::DemiBold));
            painter.drawText(QRectF(x, chart.bottom() + 2, slotWidth, 15), Qt::AlignCenter,
                             QStringLiteral("К%1").arg(index + 1));
        }

        painter.fillRect(QRectF(width() - 240, 31, 10, 8), QColor("#5e93b8"));
        painter.setPen(QColor("#8b95a3"));
        painter.setFont(QFont("Segoe UI", 7));
        painter.drawText(QRectF(width() - 226, 26, 68, 18), Qt::AlignVCenter, QStringLiteral("K задано"));
        painter.fillRect(QRectF(width() - 145, 31, 10, 8), QColor("#58bdd3"));
        painter.drawText(QRectF(width() - 131, 26, 105, 18), Qt::AlignVCenter, QStringLiteral("K рассчитано"));

        const QRectF matrix(48, 232, width() - 60, height() - 278);
        painter.setPen(QColor("#dfe6ee"));
        painter.setFont(QFont("Segoe UI", 9, QFont::DemiBold));
        painter.drawText(QRectF(matrix.left(), 207, matrix.width(), 20), Qt::AlignLeft,
                         QStringLiteral("Матрица выполненных точек · столбцы: 7 коэффициентов внутри каждой частоты"));

        const auto gs = gains();
        const auto fs = frequencies();
        const double rowHeight = matrix.height() / 8.0;
        const double columnWidth = matrix.width() / 49.0;
        for (int frequencyIndex = 0; frequencyIndex < 7; ++frequencyIndex) {
            const double groupX = matrix.left() + frequencyIndex * 7 * columnWidth;
            painter.setPen(QColor("#8293a4"));
            painter.setFont(QFont("Segoe UI", 7));
            painter.drawText(QRectF(groupX, matrix.top() - 17, 7 * columnWidth, 14), Qt::AlignCenter,
                             QStringLiteral("%1 Гц").arg(fs[frequencyIndex], 0, 'g', 6));
            if (frequencyIndex > 0) {
                painter.setPen(QPen(QColor("#344454"), 1.2));
                painter.drawLine(QPointF(groupX, matrix.top()), QPointF(groupX, matrix.bottom()));
            }
        }

        for (int channelIndex = 0; channelIndex < 8; ++channelIndex) {
            const double rowY = matrix.top() + channelIndex * rowHeight;
            painter.setPen(channelIndex == currentChannel_ ? QColor("#9ac7ff") : QColor("#9aafbf"));
            painter.setFont(QFont("Segoe UI", 8, QFont::DemiBold));
            painter.drawText(QRectF(0, rowY, 42, rowHeight), Qt::AlignRight | Qt::AlignVCenter,
                             QStringLiteral("К%1").arg(channelIndex + 1));
            if (channelIndex == currentChannel_)
                painter.fillRect(QRectF(matrix.left(), rowY, matrix.width(), rowHeight), QColor(94, 147, 184, 18));

            for (int frequencyIndex = 0; frequencyIndex < 7; ++frequencyIndex) {
                for (int gainIndex = 0; gainIndex < 7; ++gainIndex) {
                    const int column = frequencyIndex * 7 + gainIndex;
                    const QRectF cell(matrix.left() + column * columnWidth + 1,
                                      rowY + 2,
                                      std::max(2.0, columnWidth - 2),
                                      std::max(2.0, rowHeight - 4));
                    const QString key = pointKey(gs[gainIndex], fs[frequencyIndex]);
                    const bool known = data_[channelIndex].contains(key);
                    const bool current = channelIndex == currentChannel_ && key == currentKey_;
                    painter.fillRect(cell, current ? QColor("#386f9d")
                                                   : known ? QColor("#356f83")
                                                           : QColor("#18212a"));
                    painter.setPen(current ? QPen(QColor("#9ac7ff"), 1.5)
                                           : QPen(QColor("#27313c"), 1));
                    painter.drawRect(cell);
                }
            }
        }

        painter.setPen(QColor("#8b95a3"));
        painter.setFont(QFont("Segoe UI", 7));
        painter.drawText(QRectF(matrix.left(), matrix.bottom() + 4, matrix.width(), 16), Qt::AlignLeft,
                         QStringLiteral("K: 0,25 · 0,5 · 1 · 2 · 4 · 8 · 32 мВ/пКл"));
        painter.setPen(acceptance_ == QStringLiteral("not_applied")
                           ? QColor("#d7a95b") : QColor("#9ac7ff"));
        painter.drawText(QRectF(matrix.left(), matrix.bottom() + 4, matrix.width(), 16), Qt::AlignRight,
            acceptance_ == QStringLiteral("not_applied")
                ? QStringLiteral("приёмочный критерий не применяется · результат не превращать в НОРМА")
                : acceptance_);
    }

private:
    struct Point {
        double reference = 0.0;
        double measured = 0.0;
        double outputVrms = 0.0;
        bool known = false;
    };

    static QString pointKey(double gain, double frequency)
    {
        return QStringLiteral("%1|%2")
            .arg(gain, 0, 'g', 12)
            .arg(frequency, 0, 'g', 12);
    }

    static std::array<double, 7> gains()
    {
        return {0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 32.0};
    }

    static std::array<double, 7> frequencies()
    {
        return {0.15, 20.0, 250.0, 500.0, 1800.0, 2000.0, 4000.0};
    }

    std::array<QHash<QString, Point>, 8> data_;
    int currentChannel_ = -1;
    QString currentKey_;
    double currentGain_ = 0.0;
    double currentFrequency_ = 0.0;
    double currentInputVpp_ = 0.0;
    QString acceptance_;
};
