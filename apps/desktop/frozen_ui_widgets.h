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

// Final frozen visual widgets.  They contain presentation only: verdicts and
// limits come from RunEvent/backend data and are never re-derived here.

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
        // baseline belongs to the whole overload procedure and remains visible
        // between impacts.  Only the fresh/current snapshot is reset here.
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

class YvpOverview final : public QWidget
{
public:
    explicit YvpOverview(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("yvpEightChannelOverview"));
        setMinimumHeight(300);
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
        int count = 0;
        for (const auto& row : data_) if (row.contains(currentKey_)) ++count;
        setProperty("yvpRenderedChannelCount", count);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#0f1318"));

        painter.setPen(QColor("#dfe6ee"));
        painter.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
        painter.drawText(QRectF(12, 5, width() - 24, 22), Qt::AlignLeft,
                         QStringLiteral("K задано / K измерено · общая плоскость 8 каналов"));
        painter.setPen(QColor("#8da4b8"));
        painter.setFont(QFont("Segoe UI", 8));
        painter.drawText(QRectF(12, 5, width() - 24, 22), Qt::AlignRight,
            QStringLiteral("K=%1 мВ/пКл · %2 Гц · Rigol %3 Vpp")
                .arg(currentGain_, 0, 'g', 8)
                .arg(currentFrequency_, 0, 'g', 8)
                .arg(currentInputVpp_, 0, 'g', 8));

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

        const QRectF area(50, 34, width() - 62, height() - 95);
        painter.setPen(QPen(QColor("#27313c"), 1));
        painter.drawRect(area);
        const double upper = maximum * 1.12;
        const auto y = [&](double value) {
            return area.bottom() - std::clamp(value / upper, 0.0, 1.0) * area.height();
        };
        painter.setFont(QFont("Segoe UI", 7));
        for (int tick = 0; tick <= 4; ++tick) {
            const double value = upper * tick / 4.0;
            const double yy = y(value);
            painter.setPen(QPen(QColor("#27313c"), 1, Qt::DashLine));
            painter.drawLine(QPointF(area.left(), yy), QPointF(area.right(), yy));
            painter.setPen(QColor("#8293a4"));
            painter.drawText(QRectF(0, yy - 8, 44, 16), Qt::AlignRight,
                             QString::number(value, 'g', 4));
        }

        const double slotWidth = area.width() / 8.0;
        for (int index = 0; index < 8; ++index) {
            const double x = area.left() + index * slotWidth;
            if (index == currentChannel_)
                painter.fillRect(QRectF(x, area.top(), slotWidth, area.height()), QColor(94, 147, 184, 32));
            const auto& point = points[index];
            if (point.known) {
                const double sentTop = y(point.reference);
                const double measuredTop = y(point.measured);
                painter.fillRect(QRectF(x + slotWidth * .18, sentTop,
                                        std::max(4.0, slotWidth * .25), area.bottom() - sentTop),
                                 QColor("#5e93b8"));
                painter.fillRect(QRectF(x + slotWidth * .55, measuredTop,
                                        std::max(4.0, slotWidth * .25), area.bottom() - measuredTop),
                                 QColor("#58bdd3"));
                painter.setPen(QColor("#dce6ef"));
                painter.setFont(QFont("Segoe UI", 8, QFont::DemiBold));
                painter.drawText(QRectF(x, area.bottom() + 20, slotWidth, 16), Qt::AlignCenter,
                                 QStringLiteral("%1 Vrms").arg(point.outputVrms, 0, 'f', 3));
                const double error = point.reference != 0.0
                    ? (point.measured - point.reference) / point.reference * 100.0 : 0.0;
                painter.setPen(QColor("#8da4b8"));
                painter.setFont(QFont("Segoe UI", 7));
                painter.drawText(QRectF(x, area.bottom() + 36, slotWidth, 15), Qt::AlignCenter,
                                 QStringLiteral("%1%2 %")
                                     .arg(error >= 0.0 ? QStringLiteral("+") : QString())
                                     .arg(error, 0, 'f', 2));
            }
            painter.setPen(index == currentChannel_ ? QColor("#9ac7ff") : QColor("#9aafbf"));
            painter.setFont(QFont("Segoe UI", 8, QFont::DemiBold));
            painter.drawText(QRectF(x, area.bottom() + 3, slotWidth, 15), Qt::AlignCenter,
                             QStringLiteral("К%1").arg(index + 1));
        }

        painter.fillRect(QRectF(width() - 250, 30, 10, 8), QColor("#5e93b8"));
        painter.setPen(QColor("#8b95a3"));
        painter.setFont(QFont("Segoe UI", 7));
        painter.drawText(QRectF(width() - 236, 25, 68, 18), Qt::AlignVCenter, QStringLiteral("задано"));
        painter.fillRect(QRectF(width() - 160, 30, 10, 8), QColor("#58bdd3"));
        painter.drawText(QRectF(width() - 146, 25, 78, 18), Qt::AlignVCenter, QStringLiteral("измерено"));

        painter.setPen(acceptance_ == QStringLiteral("not_applied")
                           ? QColor("#8da4b8") : QColor("#9ac7ff"));
        painter.drawText(QRectF(area.left(), height() - 20, area.width(), 16), Qt::AlignRight,
            acceptance_ == QStringLiteral("not_applied")
                ? QStringLiteral("критерий приёмки не применён")
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

    std::array<QHash<QString, Point>, 8> data_;
    int currentChannel_ = -1;
    QString currentKey_;
    double currentGain_ = 0.0;
    double currentFrequency_ = 0.0;
    double currentInputVpp_ = 0.0;
    QString acceptance_;
};
