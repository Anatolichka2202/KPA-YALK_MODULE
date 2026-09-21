#pragma once

#include "ubsi_ui_model.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ubsi::ui {

namespace palette {
inline const QColor bg{"#08131d"};
inline const QColor panel{"#102333"};
inline const QColor panel2{"#132a3d"};
inline const QColor panel3{"#0e1e2c"};
inline const QColor line{"#264257"};
inline const QColor lineSoft{"#1a3346"};
inline const QColor text{"#eaf4fb"};
inline const QColor muted{"#8ea6b7"};
inline const QColor dim{"#61788a"};
inline const QColor blue{"#2e7de9"};
inline const QColor blue2{"#58a5ff"};
inline const QColor green{"#35cf79"};
inline const QColor red{"#ef5a5a"};
inline const QColor amber{"#e1ad46"};
inline const QColor cyan{"#61d5e8"};
}

inline QColor stateColor(VerificationState state)
{
    switch (state) {
    case VerificationState::Norma: return palette::green;
    case VerificationState::NeNorma:
    case VerificationState::Error: return palette::red;
    case VerificationState::Incomplete:
    case VerificationState::Stopped: return palette::amber;
    case VerificationState::Pending: return palette::dim;
    }
    return palette::dim;
}

namespace detail {

inline double finiteOr(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

inline double zoomFactor(const QWheelEvent* event)
{
    return event->angleDelta().y() >= 0 ? 1.18 : 0.84;
}

inline QColor translucent(const QColor& color, int alpha)
{
    return QColor(color.red(), color.green(), color.blue(), alpha);
}

struct HitRegion {
    QRectF rect;
    int key = 0;
    QString tooltip;
};

inline int hitKey(const QVector<HitRegion>& hits, const QPointF& position)
{
    for (const auto& hit : hits) if (hit.rect.contains(position)) return hit.key;
    return 0;
}

inline QString hitTooltip(const QVector<HitRegion>& hits, const QPointF& position)
{
    for (const auto& hit : hits) if (hit.rect.contains(position)) return hit.tooltip;
    return {};
}

} // namespace detail

class HistoryPlot final : public QWidget
{
public:
    explicit HistoryPlot(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(120);
    }

    void configure(QString title, QString unit, QString firstName, QString secondName = {})
    {
        title_ = std::move(title);
        unit_ = std::move(unit);
        firstName_ = std::move(firstName);
        secondName_ = std::move(secondName);
        update();
    }

    void setSeries(const QVector<double>& first, const QVector<double>& second = {})
    {
        first_ = first;
        second_ = second;
        update();
    }

    void append(double first, double second = std::numeric_limits<double>::quiet_NaN())
    {
        first_.push_back(first);
        if (std::isfinite(second)) second_.push_back(second);
        while (first_.size() > 240) first_.removeFirst();
        while (second_.size() > 240) second_.removeFirst();
        update();
    }

    void clear()
    {
        first_.clear();
        second_.clear();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette::panel3);

        p.setPen(palette::text);
        p.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        p.drawText(QRectF(12, 6, width() - 24, 20), Qt::AlignLeft | Qt::AlignVCenter, title_);

        const QRectF area(52, 32, std::max(20, width() - 66), std::max(30, height() - 48));
        p.setPen(QPen(palette::line, 1));
        p.drawRect(area);
        for (int tick = 1; tick < 4; ++tick) {
            const double y = area.top() + tick * area.height() / 4.0;
            p.setPen(QPen(palette::lineSoft, 1, Qt::DashLine));
            p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }

        QVector<double> finite;
        for (double value : first_) if (std::isfinite(value)) finite.push_back(value);
        for (double value : second_) if (std::isfinite(value)) finite.push_back(value);
        if (finite.isEmpty()) {
            p.setPen(palette::dim);
            p.setFont(QFont(QStringLiteral("Segoe UI"), 9));
            p.drawText(area, Qt::AlignCenter, QStringLiteral("ожидание измерения"));
            return;
        }

        const auto mm = std::minmax_element(finite.begin(), finite.end());
        double lo = *mm.first;
        double hi = *mm.second;
        const double pad = std::max((hi - lo) * .15, std::max(.001, std::abs(hi) * .01));
        lo -= pad;
        hi += pad;
        if (!(hi > lo)) hi = lo + 1.0;

        const auto yFor = [&](double value) {
            return area.bottom() - (value - lo) / (hi - lo) * area.height();
        };

        p.setFont(QFont(QStringLiteral("Segoe UI"), 7));
        for (int tick = 0; tick <= 4; ++tick) {
            const double value = lo + (hi - lo) * tick / 4.0;
            const double y = yFor(value);
            p.setPen(palette::muted);
            p.drawText(QRectF(0, y - 8, 46, 16), Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(value, 'f', unit_ == QStringLiteral("А") ? 3 : 2));
        }

        const int count = std::max(first_.size(), second_.size());
        const auto drawSeries = [&](const QVector<double>& values, const QColor& color) {
            if (values.isEmpty()) return;
            QPainterPath path;
            bool started = false;
            for (int i = 0; i < values.size(); ++i) {
                if (!std::isfinite(values[i])) { started = false; continue; }
                const double x = area.left()
                    + (count <= 1 ? area.width() : i * area.width() / double(count - 1));
                const QPointF point(x, yFor(values[i]));
                if (!started) { path.moveTo(point); started = true; }
                else path.lineTo(point);
            }
            p.setPen(QPen(color, 2));
            p.drawPath(path);
        };
        drawSeries(first_, palette::blue2);
        drawSeries(second_, palette::cyan);

        if (!firstName_.isEmpty()) {
            p.setFont(QFont(QStringLiteral("Segoe UI"), 8));
            p.setPen(palette::muted);
            QString legend = firstName_;
            if (!secondName_.isEmpty()) legend += QStringLiteral("   ·   ") + secondName_;
            p.drawText(QRectF(area.left(), 6, area.width(), 18),
                       Qt::AlignRight | Qt::AlignVCenter, legend);
        }
    }

private:
    QString title_, unit_, firstName_, secondName_;
    QVector<double> first_, second_;
};

class InitialStateView final : public QWidget
{
public:
    explicit InitialStateView(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("yalkInitialStateGrid"));
        setMinimumHeight(360);
        setMouseTracking(true);
    }

    void setFrame(const QVector<InitialChannel>& channels)
    {
        channels_ = channels;
        int measured = 0;
        int failed = 0;
        for (const auto& channel : channels_) {
            if (channel.verification != VerificationState::Pending) ++measured;
            if (channel.verification == VerificationState::NeNorma
                || channel.verification == VerificationState::Error) ++failed;
        }
        setProperty("initialMeasurementCount", measured);
        setProperty("initialFailureCount", failed);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette::panel3);
        const int cols = 10;
        const int rows = 8;
        const double gap = 4.0;
        const double margin = 10.0;
        const double cw = (width() - 2 * margin - (cols - 1) * gap) / cols;
        const double ch = (height() - 2 * margin - (rows - 1) * gap) / rows;
        for (int i = 0; i < 80; ++i) {
            const QRectF cell(margin + (i % cols) * (cw + gap),
                              margin + (i / cols) * (ch + gap), cw, ch);
            const bool valid = i < channels_.size();
            const auto channel = valid ? channels_[i] : InitialChannel{};
            QColor fill = palette::panel;
            QColor border = palette::line;
            if (valid && channel.verification != VerificationState::Pending) {
                const QColor state = stateColor(channel.verification);
                fill = detail::translucent(state, 32);
                border = state;
            }
            p.setPen(QPen(border, 1));
            p.setBrush(fill);
            p.drawRoundedRect(cell, 4, 4);
            p.setBrush(Qt::NoBrush);
            p.setPen(palette::muted);
            p.setFont(QFont(QStringLiteral("Segoe UI"), 7));
            p.drawText(cell.adjusted(5, 3, -5, -3), Qt::AlignTop | Qt::AlignLeft,
                       valid ? QString::number(channel.physicalAddress) : QStringLiteral("—"));
            if (!valid || channel.verification == VerificationState::Pending) continue;
            p.setPen(palette::text);
            p.setFont(QFont(QStringLiteral("Segoe UI"), 8, QFont::DemiBold));
            p.drawText(cell.adjusted(5, 15, -5, -3), Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("%1 В\nD %2")
                           .arg(channel.analogV, 0, 'f', 2)
                           .arg(channel.logic));
        }
    }

private:
    QVector<InitialChannel> channels_;
};

class ChannelPlane final : public QWidget
{
public:
    enum class Kind { Yalk, Ytp, PassivePower };

    explicit ChannelPlane(Kind kind, QWidget* parent = nullptr)
        : QWidget(parent), kind_(kind)
    {
        setMinimumHeight(340);
        setMouseTracking(true);
        setCursor(kind_ == Kind::PassivePower ? Qt::ArrowCursor : Qt::CrossCursor);
        if (kind_ == Kind::Yalk) setObjectName(QStringLiteral("yalkChannelHistogram"));
        else if (kind_ == Kind::Ytp) setObjectName(QStringLiteral("ytpChannelHistogram"));
        else setObjectName(QStringLiteral("powerYalkOverview"));
        publishInteractionState();
    }

    void setYalkFrame(const YalkAnalogFrame& frame)
    {
        yalk_ = frame;
        if (frame.pinnedChannel > 0) pinnedChannel_ = frame.pinnedChannel;
        int count = 0;
        int warnings = 0;
        for (const auto& channel : frame.channels) {
            if (std::isfinite(channel.currentV)) ++count;
            if (channel.warning) ++warnings;
        }
        setProperty("renderedChannelCount", count);
        setProperty("backgroundChannelCount", count);
        setProperty("warningChannelCount", warnings);
        publishInteractionState();
        update();
    }

    void setYtpFrame(const YtpFrame& frame)
    {
        ytp_ = frame;
        if (frame.pinnedChannel > 0) pinnedChannel_ = frame.pinnedChannel;
        int count = 0;
        for (const auto& channel : frame.channels)
            if (std::isfinite(channel.currentOhm)) ++count;
        setProperty("renderedChannelCount", count);
        publishInteractionState();
        update();
    }

    void setPassiveValues(const QVector<double>& values, bool fresh)
    {
        passive_ = values;
        setProperty("fresh", fresh);
        setEnabled(fresh);
        setProperty("renderedChannelCount", std::min<qsizetype>(80, values.size()));
        update();
    }

    void resetView()
    {
        zoomScale_ = 1.0;
        panUnits_ = 0.0;
        fullScale_ = false;
        publishInteractionState();
        update();
    }

    void setFullScale(bool enabled)
    {
        if (kind_ == Kind::PassivePower) return;
        fullScale_ = enabled;
        zoomScale_ = 1.0;
        panUnits_ = 0.0;
        publishInteractionState();
        update();
    }

    void toggleFullScale() { setFullScale(!fullScale_); }
    bool fullScale() const noexcept { return fullScale_; }
    int pinnedChannel() const noexcept { return pinnedChannel_; }

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        if (kind_ == Kind::PassivePower) { event->ignore(); return; }
        fullScale_ = false;
        zoomScale_ = std::clamp(zoomScale_ * detail::zoomFactor(event), 1.0, 12.0);
        if (zoomScale_ <= 1.0001) panUnits_ = 0.0;
        publishInteractionState();
        update();
        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && kind_ != Kind::PassivePower) {
            resetView();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && kind_ != Kind::PassivePower) {
            pressPos_ = event->position();
            dragStartPan_ = panUnits_;
            dragging_ = true;
            dragMoved_ = false;
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (dragging_ && (event->buttons() & Qt::LeftButton) && kind_ != Kind::PassivePower) {
            const double dy = event->position().y() - pressPos_.y();
            if (std::abs(dy) >= 2.0) dragMoved_ = true;
            if (dragMoved_) {
                const double height = std::max(40.0, double(this->height() - 70));
                panUnits_ = dragStartPan_ + dy / height * lastViewWidth_;
                fullScale_ = false;
                publishInteractionState();
                update();
                event->accept();
                return;
            }
        }

        const QString tip = detail::hitTooltip(hits_, event->position());
        if (!tip.isEmpty()) QToolTip::showText(event->globalPosition().toPoint(), tip, this);
        else QToolTip::hideText();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && dragging_) {
            if (!dragMoved_) {
                const int key = detail::hitKey(hits_, event->position());
                if (key > 0) pinnedChannel_ = pinnedChannel_ == key ? 0 : key;
            }
            dragging_ = false;
            dragMoved_ = false;
            publishInteractionState();
            update();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette::panel3);
        hits_.clear();

        const QRectF area(54, 30, std::max(30, width() - 68), std::max(40, height() - 70));
        p.setPen(QPen(palette::line, 1));
        p.drawRect(area);

        struct Value {
            int key;
            double value;
            double minimum;
            double maximum;
            double errorPercent;
            VerificationState state;
            bool warning;
        };
        QVector<Value> values;
        QString unit;
        double nominalTop = 1.0;
        int activeChannel = 0;

        if (kind_ == Kind::Yalk) {
            unit = QStringLiteral("В");
            nominalTop = 6.2;
            activeChannel = yalk_.stimulatedChannel;
            for (const auto& channel : yalk_.channels) {
                values.push_back({channel.physicalAddress, channel.currentV,
                                  channel.minimumV, channel.maximumV,
                                  channel.reducedErrorPercent,
                                  channel.verification, channel.warning});
            }
        } else if (kind_ == Kind::Ytp) {
            unit = QStringLiteral("Ом");
            nominalTop = 240.0;
            activeChannel = ytp_.testedChannel;
            for (const auto& channel : ytp_.channels) {
                values.push_back({channel.channel, channel.currentOhm,
                                  channel.minimumOhm, channel.maximumOhm,
                                  std::numeric_limits<double>::quiet_NaN(),
                                  channel.verification, false});
            }
        } else {
            unit = QStringLiteral("В");
            nominalTop = 6.2;
            const auto keys = yalkPhysicalAddresses();
            for (int i = 0; i < keys.size(); ++i) {
                values.push_back({keys[i], i < passive_.size() ? passive_[i]
                                    : std::numeric_limits<double>::quiet_NaN(),
                                  std::numeric_limits<double>::quiet_NaN(),
                                  std::numeric_limits<double>::quiet_NaN(),
                                  std::numeric_limits<double>::quiet_NaN(),
                                  VerificationState::Pending, false});
            }
        }

        double observedMin = std::numeric_limits<double>::infinity();
        double observedMax = -std::numeric_limits<double>::infinity();
        for (const auto& value : values) if (std::isfinite(value.value)) {
            observedMin = std::min(observedMin,
                                   std::isfinite(value.minimum) ? value.minimum : value.value);
            observedMax = std::max(observedMax,
                                   std::isfinite(value.maximum) ? value.maximum : value.value);
        }

        double baseLo = 0.0;
        double baseHi = nominalTop;
        if (kind_ == Kind::Yalk && !fullScale_ && std::isfinite(yalk_.pointV)) {
            const double center = detail::finiteOr(yalk_.actualReferenceV7, yalk_.pointV);
            const double observedSpan = std::isfinite(observedMin) && std::isfinite(observedMax)
                ? observedMax - observedMin : 0.0;
            const double half = std::max(.06, observedSpan * 1.15);
            baseLo = center - half;
            baseHi = center + half;
        } else if (kind_ == Kind::Ytp && !fullScale_
                   && std::isfinite(ytp_.resistancePointOhm)) {
            const double center = detail::finiteOr(ytp_.actualReferenceOhm, ytp_.resistancePointOhm);
            const double observedSpan = std::isfinite(observedMin) && std::isfinite(observedMax)
                ? observedMax - observedMin : 0.0;
            const double half = std::max(0.6, observedSpan * 1.25);
            baseLo = center - half;
            baseHi = center + half;
        } else if (kind_ != Kind::Yalk && kind_ != Kind::Ytp && std::isfinite(observedMax)) {
            baseHi = std::max(baseHi, observedMax * 1.05);
        }

        if (!(baseHi > baseLo)) { baseLo = 0.0; baseHi = nominalTop; }
        const double baseWidth = baseHi - baseLo;
        const double viewWidth = baseWidth / std::max(1.0, zoomScale_);
        const double center = (baseLo + baseHi) / 2.0 + panUnits_;
        const double lo = center - viewWidth / 2.0;
        const double hi = center + viewWidth / 2.0;
        lastViewWidth_ = std::max(1e-12, viewWidth);
        setProperty("viewMinimum", lo);
        setProperty("viewMaximum", hi);

        const auto yFor = [&](double value) {
            return area.bottom() - std::clamp((value - lo) / std::max(1e-12, hi - lo), 0.0, 1.0)
                * area.height();
        };

        for (int tick = 1; tick < 4; ++tick) {
            const double y = area.top() + tick * area.height() / 4.0;
            p.setPen(QPen(palette::lineSoft, 1, Qt::DashLine));
            p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }

        p.setFont(QFont(QStringLiteral("Segoe UI"), 7));
        for (int tick = 0; tick <= 4; ++tick) {
            const double value = lo + (hi - lo) * tick / 4.0;
            const double y = yFor(value);
            p.setPen(palette::muted);
            p.drawText(QRectF(0, y - 8, 48, 16), Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(value, 'f', unit == QStringLiteral("Ом") ? 1 : 3));
        }

        const double cell = area.width() / std::max<qsizetype>(1, values.size());
        int rendered = 0;
        for (int i = 0; i < values.size(); ++i) {
            const auto& value = values[i];
            const double x = area.left() + i * cell;
            const QRectF hit(x, area.top(), cell, area.height() + 24);
            const bool active = value.key == activeChannel;
            const bool pinned = value.key == pinnedChannel_;
            if (active) p.fillRect(QRectF(x, area.top(), cell, area.height()),
                                   detail::translucent(palette::blue, 34));
            if (pinned) {
                p.setPen(QPen(palette::blue2, 2));
                p.drawRect(QRectF(x + 1, area.top() + 1, std::max(1.0, cell - 2), area.height() - 2));
            }

            if (std::isfinite(value.value)) {
                ++rendered;
                const QColor color = value.state == VerificationState::NeNorma
                      || value.state == VerificationState::Error
                        ? palette::red : palette::cyan;
                const double top = yFor(value.value);
                p.fillRect(QRectF(x + cell * .20, top,
                                  std::max(2.0, cell * .60), area.bottom() - top), color);
                if (std::isfinite(value.minimum) && std::isfinite(value.maximum)) {
                    p.setPen(QPen(palette::text, 1));
                    p.drawLine(QPointF(x + cell / 2, yFor(value.minimum)),
                               QPointF(x + cell / 2, yFor(value.maximum)));
                }
                hits_.push_back({hit, value.key,
                    QStringLiteral("Канал %1\n%2 %3\nmin…max %4…%5%6%7")
                        .arg(value.key)
                        .arg(value.value, 0, 'f', unit == QStringLiteral("Ом") ? 2 : 4)
                        .arg(unit)
                        .arg(value.minimum, 0, 'f', unit == QStringLiteral("Ом") ? 2 : 4)
                        .arg(value.maximum, 0, 'f', unit == QStringLiteral("Ом") ? 2 : 4)
                        .arg(std::isfinite(value.errorPercent)
                            ? QStringLiteral("\nПогрешность %1 %").arg(value.errorPercent, 0, 'f', 3)
                            : QString())
                        .arg(pinned ? QStringLiteral("\nзакреплён") : QString())});
            }

            if (i == 0 || i == values.size() - 1
                || (i + 1) % (values.size() > 40 ? 5 : 2) == 0) {
                p.setPen(palette::muted);
                p.setFont(QFont(QStringLiteral("Segoe UI"), 7));
                p.drawText(QRectF(x - cell, area.bottom() + 4, cell * 3, 15),
                           Qt::AlignCenter, QString::number(value.key));
            }
        }

        setProperty("renderedChannelCount", rendered);
        if (rendered == 0) {
            p.setPen(palette::dim);
            p.setFont(QFont(QStringLiteral("Segoe UI"), 9));
            p.drawText(area, Qt::AlignCenter, QStringLiteral("ожидание поканальных данных"));
        }
    }

private:
    void publishInteractionState()
    {
        setProperty("zoomScale", zoomScale_);
        setProperty("panUnits", panUnits_);
        setProperty("fullScale", fullScale_);
        setProperty("pinnedChannel", pinnedChannel_);
    }

    Kind kind_;
    YalkAnalogFrame yalk_;
    YtpFrame ytp_;
    QVector<double> passive_;
    QVector<detail::HitRegion> hits_;
    double zoomScale_ = 1.0;
    double panUnits_ = 0.0;
    double dragStartPan_ = 0.0;
    double lastViewWidth_ = 1.0;
    QPointF pressPos_;
    bool fullScale_ = false;
    bool dragging_ = false;
    bool dragMoved_ = false;
    int pinnedChannel_ = 0;
};

class ContactPlane final : public QWidget
{
public:
    explicit ContactPlane(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("yalkContactThresholdOverview"));
        setMinimumHeight(360);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        publishInteractionState();
    }

    void setFrame(const YalkContactFrame& frame, int totalMeasurements)
    {
        frame_ = frame;
        if (frame.pinnedChannel > 0) pinnedChannel_ = frame.pinnedChannel;
        setProperty("contactMeasurementCount", totalMeasurements);
        publishInteractionState();
        update();
    }

    void resetView()
    {
        zoomScale_ = 1.0;
        panUnits_ = 0.0;
        publishInteractionState();
        update();
    }

    int pinnedChannel() const noexcept { return pinnedChannel_; }

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        zoomScale_ = std::clamp(zoomScale_ * detail::zoomFactor(event), 1.0, 12.0);
        if (zoomScale_ <= 1.0001) panUnits_ = 0.0;
        publishInteractionState();
        update();
        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            resetView();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            pressPos_ = event->position();
            dragStartPan_ = panUnits_;
            dragging_ = true;
            dragMoved_ = false;
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (dragging_ && (event->buttons() & Qt::LeftButton)) {
            const double dy = event->position().y() - pressPos_.y();
            if (std::abs(dy) >= 2.0) dragMoved_ = true;
            if (dragMoved_) {
                const double height = std::max(40.0, double(this->height() * .70));
                panUnits_ = dragStartPan_ + dy / height * lastViewWidth_;
                publishInteractionState();
                update();
                event->accept();
                return;
            }
        }
        const QString tip = detail::hitTooltip(hits_, event->position());
        if (!tip.isEmpty()) QToolTip::showText(event->globalPosition().toPoint(), tip, this);
        else QToolTip::hideText();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && dragging_) {
            if (!dragMoved_) {
                const int key = detail::hitKey(hits_, event->position());
                if (key > 0) pinnedChannel_ = pinnedChannel_ == key ? 0 : key;
            }
            dragging_ = false;
            dragMoved_ = false;
            publishInteractionState();
            update();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette::panel3);
        hits_.clear();

        const QRectF analog(54, 28, std::max(30, width() - 68),
                            std::max(30.0, (height() - 72) * .70));
        const QRectF digital(54, analog.bottom() + 10, std::max(30, width() - 68),
                             std::max(20.0, (height() - 72) * .30 - 10));
        p.setPen(QPen(palette::line, 1));
        p.drawRect(analog);
        p.drawRect(digital);

        double observedMin = std::numeric_limits<double>::infinity();
        double observedMax = -std::numeric_limits<double>::infinity();
        for (const auto& channel : frame_.channels) if (std::isfinite(channel.currentV)) {
            observedMin = std::min(observedMin,
                                   std::isfinite(channel.minimumV) ? channel.minimumV : channel.currentV);
            observedMax = std::max(observedMax,
                                   std::isfinite(channel.maximumV) ? channel.maximumV : channel.currentV);
        }
        const double center = detail::finiteOr(frame_.actualReferenceV7,
                                               detail::finiteOr(frame_.pointV, 1.25));
        const double observedSpan = std::isfinite(observedMin) && std::isfinite(observedMax)
            ? observedMax - observedMin : 0.0;
        const double half = std::max(.12, observedSpan * 1.15);
        const double baseLo = center - half;
        const double baseHi = center + half;
        const double baseWidth = std::max(1e-9, baseHi - baseLo);
        const double viewWidth = baseWidth / std::max(1.0, zoomScale_);
        const double viewCenter = (baseLo + baseHi) / 2.0 + panUnits_;
        const double lo = viewCenter - viewWidth / 2.0;
        const double hi = viewCenter + viewWidth / 2.0;
        lastViewWidth_ = viewWidth;
        setProperty("viewMinimum", lo);
        setProperty("viewMaximum", hi);

        const auto yFor = [&](double value) {
            return analog.bottom() - std::clamp((value - lo) / std::max(1e-12, hi - lo), 0.0, 1.0)
                * analog.height();
        };

        for (int tick = 1; tick < 4; ++tick) {
            const double y = analog.top() + tick * analog.height() / 4.0;
            p.setPen(QPen(palette::lineSoft, 1, Qt::DashLine));
            p.drawLine(QPointF(analog.left(), y), QPointF(analog.right(), y));
        }

        const double cell = analog.width() / std::max<qsizetype>(1, frame_.channels.size());
        for (int i = 0; i < frame_.channels.size(); ++i) {
            const auto& channel = frame_.channels[i];
            const double x = analog.left() + i * cell;
            const bool active = channel.physicalAddress == frame_.stimulatedChannel;
            const bool pinned = channel.physicalAddress == pinnedChannel_;
            if (active) {
                p.fillRect(QRectF(x, analog.top(), cell, digital.bottom() - analog.top()),
                           detail::translucent(palette::blue, 34));
            }
            if (pinned) {
                p.setPen(QPen(palette::blue2, 2));
                p.drawRect(QRectF(x + 1, analog.top() + 1, std::max(1.0, cell - 2),
                                  digital.bottom() - analog.top() - 2));
            }
            if (std::isfinite(channel.currentV)) {
                const double top = yFor(channel.currentV);
                const QColor color = channel.verification == VerificationState::NeNorma
                    || channel.verification == VerificationState::Error ? palette::red : palette::cyan;
                p.fillRect(QRectF(x + cell * .2, top,
                                  std::max(2.0, cell * .6), analog.bottom() - top), color);
                if (std::isfinite(channel.minimumV) && std::isfinite(channel.maximumV)) {
                    p.setPen(QPen(palette::text, 1));
                    p.drawLine(QPointF(x + cell / 2, yFor(channel.minimumV)),
                               QPointF(x + cell / 2, yFor(channel.maximumV)));
                }
            }
            if (channel.logic >= 0) {
                p.setPen(channel.logic ? palette::green : palette::muted);
                p.setFont(QFont(QStringLiteral("Segoe UI"), 7, QFont::DemiBold));
                p.drawText(QRectF(x, digital.top(), cell, digital.height()),
                           Qt::AlignCenter, QString::number(channel.logic));
            }
            hits_.push_back({QRectF(x, analog.top(), cell, digital.bottom() - analog.top()),
                             channel.physicalAddress,
                             QStringLiteral("Канал %1\n%2 В\nD %3\n%4")
                                .arg(channel.physicalAddress)
                                .arg(channel.currentV, 0, 'f', 4)
                                .arg(channel.logic)
                                .arg(verificationText(channel.verification))});
            if (i == 0 || i == frame_.channels.size() - 1 || (i + 1) % 5 == 0) {
                p.setPen(palette::muted);
                p.setFont(QFont(QStringLiteral("Segoe UI"), 7));
                p.drawText(QRectF(x - cell, analog.bottom() + 1, cell * 3, 14),
                           Qt::AlignCenter, QString::number(channel.physicalAddress));
            }
        }
        p.setPen(palette::muted);
        p.setFont(QFont(QStringLiteral("Segoe UI"), 8));
        p.drawText(QRectF(2, analog.top(), 48, 18), Qt::AlignRight, QStringLiteral("U, В"));
        p.drawText(QRectF(2, digital.top(), 48, 18), Qt::AlignRight, QStringLiteral("D"));
    }

private:
    void publishInteractionState()
    {
        setProperty("zoomScale", zoomScale_);
        setProperty("panUnits", panUnits_);
        setProperty("pinnedChannel", pinnedChannel_);
    }

    YalkContactFrame frame_;
    QVector<detail::HitRegion> hits_;
    double zoomScale_ = 1.0;
    double panUnits_ = 0.0;
    double dragStartPan_ = 0.0;
    double lastViewWidth_ = 1.0;
    QPointF pressPos_;
    bool dragging_ = false;
    bool dragMoved_ = false;
    int pinnedChannel_ = 0;
};

class OverloadPlane final : public QWidget
{
public:
    explicit OverloadPlane(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("yalkOverloadOverview"));
        setMinimumHeight(400);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        publishInteractionState();
    }

    void setFrame(const YalkOverloadFrame& frame)
    {
        frame_ = frame;
        int measured = 0;
        for (const auto& channel : frame.channels)
            if (std::isfinite(channel.currentCode)) ++measured;
        setProperty("overloadMeasurementCount", measured);
        update();
    }

    void resetView()
    {
        zoomScale_ = 1.0;
        publishInteractionState();
        update();
    }

    int pinnedChannel() const noexcept { return pinnedChannel_; }

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        zoomScale_ = std::clamp(zoomScale_ * detail::zoomFactor(event), 1.0, 12.0);
        publishInteractionState();
        update();
        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            resetView();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const QString tip = detail::hitTooltip(hits_, event->position());
        if (!tip.isEmpty()) QToolTip::showText(event->globalPosition().toPoint(), tip, this);
        else QToolTip::hideText();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            const int key = detail::hitKey(hits_, event->position());
            if (key > 0) pinnedChannel_ = pinnedChannel_ == key ? 0 : key;
            publishInteractionState();
            update();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette::panel3);
        hits_.clear();
        const QRectF area(54, 42, std::max(30, width() - 68), std::max(40, height() - 82));
        p.setPen(QPen(palette::line, 1));
        p.drawRect(area);

        double maxDelta = 0.0;
        bool hasMeasurements = false;
        for (const auto& channel : frame_.channels) {
            if (std::isfinite(channel.currentCode)) {
                hasMeasurements = true;
                maxDelta = std::max(maxDelta, std::abs(channel.deltaCode));
            }
        }
        if (!hasMeasurements) {
            p.setPen(palette::dim);
            p.drawText(area, Qt::AlignCenter, QStringLiteral("ожидание baseline / current"));
            return;
        }
        const double criterion = std::max(0.1, frame_.criterionCode);
        const double span = std::max({1.0, criterion * 1.35, maxDelta * 1.15})
            / std::max(1.0, zoomScale_);
        const double lo = -span;
        const double hi = span;
        setProperty("viewMinimum", lo);
        setProperty("viewMaximum", hi);
        const auto yFor = [&](double value) {
            return area.bottom() - std::clamp((value - lo) / std::max(1e-12, hi - lo), 0.0, 1.0)
                * area.height();
        };

        p.setPen(QPen(palette::lineSoft, 1, Qt::DashLine));
        p.drawLine(QPointF(area.left(), yFor(0.0)), QPointF(area.right(), yFor(0.0)));
        p.setPen(QPen(palette::red, 1, Qt::DashLine));
        p.drawLine(QPointF(area.left(), yFor(criterion)), QPointF(area.right(), yFor(criterion)));
        p.drawLine(QPointF(area.left(), yFor(-criterion)), QPointF(area.right(), yFor(-criterion)));

        const int channelCount = frame_.channels.size();
        const double cell = area.width() / std::max(1, channelCount);
        QPointF previousPoint;
        bool hasPreviousPoint = false;
        for (int i = 0; i < channelCount; ++i) {
            const auto& channel = frame_.channels[i];
            const double x = area.left() + i * cell;
            const bool active = channel.physicalAddress == frame_.stressedChannel;
            const bool pinned = channel.physicalAddress == pinnedChannel_;
            if (active) {
                p.fillRect(QRectF(x, area.top(), cell, area.height()),
                           detail::translucent(palette::blue, 45));
            }
            if (pinned) {
                p.setPen(QPen(palette::blue2, 2));
                p.drawRect(QRectF(x + 1, area.top() + 1, std::max(1.0, cell - 2), area.height() - 2));
            }
            if (std::isfinite(channel.currentCode)) {
                const QPointF point(x + cell * .5, yFor(channel.deltaCode));
                const QColor color = channel.verification == VerificationState::NeNorma
                    || channel.verification == VerificationState::Error ? palette::red : palette::cyan;
                if (hasPreviousPoint) {
                    p.setPen(QPen(palette::cyan, 1.4));
                    p.drawLine(previousPoint, point);
                }
                p.setPen(Qt::NoPen);
                p.setBrush(color);
                p.drawEllipse(point, std::max(2.0, cell * .18), std::max(2.0, cell * .18));
                previousPoint = point;
                hasPreviousPoint = true;
                hits_.push_back({QRectF(x, area.top(), cell, area.height()), channel.physicalAddress,
                    QStringLiteral("Канал %1\nИсходный код %2\nТекущий код %3\nΔcode %4\n%5")
                        .arg(channel.physicalAddress)
                        .arg(channel.baselineCode, 0, 'f', 1)
                        .arg(channel.currentCode, 0, 'f', 1)
                        .arg(channel.deltaCode, 0, 'f', 1)
                        .arg(verificationText(channel.verification))});
            }
            if (i == 0 || i == channelCount - 1 || (i + 1) % 5 == 0) {
                p.setPen(palette::muted);
                p.setFont(QFont(QStringLiteral("Segoe UI"), 7));
                p.drawText(QRectF(x - cell, area.bottom() + 3, cell * 3, 14),
                           Qt::AlignCenter, QString::number(channel.physicalAddress));
            }
        }

        p.setPen(palette::muted);
        p.setFont(QFont(QStringLiteral("Segoe UI"), 8));
        p.drawText(QRectF(area.left(), 8, area.width(), 20), Qt::AlignLeft,
                   QStringLiteral("Δcode = текущий − исходный · допуск ±%1 кодов")
                       .arg(frame_.criterionCode, 0, 'f', 0));
        p.setPen(maxDelta > frame_.criterionCode ? palette::red : palette::blue2);
        p.drawText(QRectF(area.left(), 8, area.width(), 20), Qt::AlignRight,
                   QStringLiteral("%1 · канал %2 · воздействие %3/%4 · max |Δ| %5")
                       .arg(frame_.polarity)
                       .arg(frame_.stressedChannel)
                       .arg(frame_.impactIndex)
                       .arg(frame_.impactCount)
                       .arg(maxDelta, 0, 'f', 1));
    }

private:
    void publishInteractionState()
    {
        setProperty("zoomScale", zoomScale_);
        setProperty("pinnedChannel", pinnedChannel_);
    }

    YalkOverloadFrame frame_;
    QVector<detail::HitRegion> hits_;
    double zoomScale_ = 1.0;
    int pinnedChannel_ = 0;
};

class YvpPlane final : public QWidget
{
public:
    explicit YvpPlane(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("yvpEightChannelOverview"));
        setMinimumHeight(360);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        setProperty("pinnedChannel", 0);
    }

    void setFrame(const YvpFrame& frame, int completedPoints)
    {
        frame_ = frame;
        if (frame.pinnedChannel > 0) pinnedChannel_ = frame.pinnedChannel;
        int rendered = 0;
        for (const auto& channel : frame.channels)
            if (std::isfinite(channel.measuredValue) || std::isfinite(channel.calculatedGain)) ++rendered;
        setProperty("yvpRenderedChannelCount", rendered);
        setProperty("yvpCompletedPointCount", completedPoints);
        setProperty("pinnedChannel", pinnedChannel_);
        update();
    }

    int pinnedChannel() const noexcept { return pinnedChannel_; }

protected:
    void mouseMoveEvent(QMouseEvent* event) override
    {
        const QString tip = detail::hitTooltip(hits_, event->position());
        if (!tip.isEmpty()) QToolTip::showText(event->globalPosition().toPoint(), tip, this);
        else QToolTip::hideText();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            const int key = detail::hitKey(hits_, event->position());
            if (key > 0) pinnedChannel_ = pinnedChannel_ == key ? 0 : key;
            setProperty("pinnedChannel", pinnedChannel_);
            update();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette::panel3);
        hits_.clear();
        const QRectF area(48, 42, std::max(30, width() - 62), std::max(40, height() - 80));
        p.setPen(QPen(palette::line, 1));
        p.drawRect(area);

        double hi = 1.0;
        for (const auto& channel : frame_.channels) {
            if (std::isfinite(channel.stimulusValue)) hi = std::max(hi, channel.stimulusValue);
            if (std::isfinite(channel.measuredValue)) hi = std::max(hi, channel.measuredValue);
            if (std::isfinite(channel.calculatedGain)) hi = std::max(hi, channel.calculatedGain);
        }
        hi *= 1.15;
        const double cell = area.width() / 8.0;
        for (int i = 0; i < 8; ++i) {
            const YvpChannel channel = i < frame_.channels.size() ? frame_.channels[i] : YvpChannel{};
            const int key = channel.channel > 0 ? channel.channel : i + 1;
            const double x = area.left() + i * cell;
            const bool active = key == frame_.testedChannel;
            const bool pinned = key == pinnedChannel_;
            if (active) {
                p.fillRect(QRectF(x, area.top(), cell, area.height()),
                           detail::translucent(palette::blue, 35));
            }
            if (pinned) {
                p.setPen(QPen(palette::blue2, 2));
                p.drawRect(QRectF(x + 1, area.top() + 1, std::max(1.0, cell - 2), area.height() - 2));
            }
            if (std::isfinite(channel.stimulusValue)) {
                const double h = std::clamp(channel.stimulusValue / hi, 0.0, 1.0) * area.height();
                p.fillRect(QRectF(x + cell * .16, area.bottom() - h, cell * .25, h),
                           detail::translucent(palette::blue2, 130));
            }
            const double measured = std::isfinite(channel.measuredValue)
                ? channel.measuredValue : channel.calculatedGain;
            if (std::isfinite(measured)) {
                const double h = std::clamp(measured / hi, 0.0, 1.0) * area.height();
                const QColor color = channel.verification == VerificationState::NeNorma
                    || channel.verification == VerificationState::Error ? palette::red : palette::cyan;
                p.fillRect(QRectF(x + cell * .55, area.bottom() - h, cell * .25, h), color);
            }
            hits_.push_back({QRectF(x, area.top(), cell, area.height()), key,
                             QStringLiteral("Канал %1\nподано %2\nизмерено %3\nKу %4\n%5")
                                .arg(key)
                                .arg(channel.stimulusValue, 0, 'g', 6)
                                .arg(channel.measuredValue, 0, 'g', 6)
                                .arg(channel.calculatedGain, 0, 'g', 6)
                                .arg(verificationText(channel.verification))});
            p.setPen(palette::muted);
            p.setFont(QFont(QStringLiteral("Segoe UI"), 8));
            p.drawText(QRectF(x, area.bottom() + 4, cell, 16), Qt::AlignCenter, QString::number(key));
        }
        p.setPen(palette::muted);
        p.setFont(QFont(QStringLiteral("Segoe UI"), 8));
        p.drawText(QRectF(area.left(), 8, area.width(), 20), Qt::AlignLeft,
                   QStringLiteral("подано / измерено · 8 каналов"));
        p.drawText(QRectF(area.left(), 8, area.width(), 20), Qt::AlignRight,
                   QStringLiteral("Kу %1 · %2 Гц · выполнено %3/%4")
                       .arg(frame_.gain, 0, 'g', 6)
                       .arg(frame_.frequencyHz, 0, 'g', 8)
                       .arg(property("yvpCompletedPointCount").toInt())
                       .arg(frame_.pointCount));
    }

private:
    YvpFrame frame_;
    QVector<detail::HitRegion> hits_;
    int pinnedChannel_ = 0;
};

} // namespace ubsi::ui
