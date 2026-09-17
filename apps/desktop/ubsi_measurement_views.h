#pragma once

#include "ubsi_ui_model.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
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
        for (double v : first_) if (std::isfinite(v)) finite.push_back(v);
        for (double v : second_) if (std::isfinite(v)) finite.push_back(v);
        if (finite.isEmpty()) {
            p.setPen(palette::dim);
            p.setFont(QFont(QStringLiteral("Segoe UI"), 9));
            p.drawText(area, Qt::AlignCenter, QStringLiteral("ожидание измерения"));
            return;
        }

        auto mm = std::minmax_element(finite.begin(), finite.end());
        double lo = *mm.first;
        double hi = *mm.second;
        double pad = std::max((hi - lo) * .15, std::max(.001, std::abs(hi) * .01));
        if (!(hi > lo)) { lo -= pad; hi += pad; }
        else { lo -= pad; hi += pad; }
        auto yFor = [&](double value) {
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
        auto drawSeries = [&](const QVector<double>& values, const QColor& color) {
            if (values.isEmpty()) return;
            QPainterPath path;
            bool started = false;
            for (int i = 0; i < values.size(); ++i) {
                if (!std::isfinite(values[i])) { started = false; continue; }
                const double x = area.left() + (count <= 1 ? area.width() : i * area.width() / double(count - 1));
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
            p.drawText(QRectF(area.left(), 6, area.width(), 18), Qt::AlignRight | Qt::AlignVCenter, legend);
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
        int measured = 0, failed = 0;
        for (const auto& c : channels_) {
            if (c.verification != VerificationState::Pending) ++measured;
            if (c.verification == VerificationState::NeNorma || c.verification == VerificationState::Error) ++failed;
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
                fill = QColor(state.red(), state.green(), state.blue(), 32);
                border = state;
            }
            p.setPen(QPen(border, 1));
            p.setBrush(fill);
            p.drawRoundedRect(cell, 4, 4);
            p.setBrush(Qt::NoBrush);
            p.setPen(palette::muted);
            p.setFont(QFont(QStringLiteral("Segoe UI"), 7));
            p.drawText(cell.adjusted(5, 3, -5, -3), Qt::AlignTop | Qt::AlignLeft,
                       valid ? QStringLiteral("%1").arg(channel.physicalAddress) : QStringLiteral("—"));
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
    explicit ChannelPlane(Kind kind, QWidget* parent = nullptr) : QWidget(parent), kind_(kind)
    {
        setMinimumHeight(340);
        setMouseTracking(true);
        if (kind_ == Kind::Yalk) setObjectName(QStringLiteral("yalkChannelHistogram"));
        else if (kind_ == Kind::Ytp) setObjectName(QStringLiteral("ytpChannelHistogram"));
        else setObjectName(QStringLiteral("powerYalkOverview"));
    }

    void setYalkFrame(const YalkAnalogFrame& frame)
    {
        yalk_ = frame;
        int count = 0, warnings = 0;
        for (const auto& channel : frame.channels) {
            if (std::isfinite(channel.currentV)) ++count;
            if (channel.warning) ++warnings;
        }
        setProperty("renderedChannelCount", count);
        setProperty("backgroundChannelCount", count);
        setProperty("warningChannelCount", warnings);
        update();
    }

    void setYtpFrame(const YtpFrame& frame)
    {
        ytp_ = frame;
        int count = 0;
        for (const auto& channel : frame.channels) if (std::isfinite(channel.currentOhm)) ++count;
        setProperty("renderedChannelCount", count);
        update();
    }

    void setPassiveValues(const QVector<double>& values, bool fresh)
    {
        passive_ = values;
        setProperty("fresh", fresh);
        setEnabled(fresh);
        setProperty("renderedChannelCount", std::min(80, values.size()));
        update();
    }

protected:
    void mouseMoveEvent(QMouseEvent* event) override
    {
        for (const auto& hit : hits_) {
            if (!hit.first.contains(event->position())) continue;
            QToolTip::showText(event->globalPosition().toPoint(), hit.second, this, hit.first.toRect());
            return;
        }
        QToolTip::hideText();
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
        for (int tick = 1; tick < 4; ++tick) {
            const double y = area.top() + tick * area.height() / 4.0;
            p.setPen(QPen(palette::lineSoft, 1, Qt::DashLine));
            p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }

        struct V { int key; double value; double min; double max; VerificationState state; bool warning; };
        QVector<V> values;
        QString unit;
        double nominalTop = 1.0;
        if (kind_ == Kind::Yalk) {
            unit = QStringLiteral("В");
            nominalTop = 6.2;
            for (const auto& c : yalk_.channels)
                values.push_back({c.physicalAddress, c.currentV, c.minimumV, c.maximumV, c.verification, c.warning});
        } else if (kind_ == Kind::Ytp) {
            unit = QStringLiteral("Ом");
            nominalTop = std::max(240.0, std::isfinite(ytp_.resistancePointOhm) ? ytp_.resistancePointOhm * 1.15 : 240.0);
            for (const auto& c : ytp_.channels)
                values.push_back({c.channel, c.currentOhm, c.minimumOhm, c.maximumOhm, c.verification, false});
        } else {
            unit = QStringLiteral("В");
            nominalTop = 6.2;
            const auto keys = yalkPhysicalAddresses();
            for (int i = 0; i < keys.size(); ++i)
                values.push_back({keys[i], i < passive_.size() ? passive_[i] : std::numeric_limits<double>::quiet_NaN(),
                                  std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
                                  VerificationState::Pending, false});
        }

        double observedMin = std::numeric_limits<double>::infinity();
        double observedMax = -std::numeric_limits<double>::infinity();
        for (const auto& v : values) if (std::isfinite(v.value)) {
            observedMin = std::min(observedMin, std::isfinite(v.min) ? v.min : v.value);
            observedMax = std::max(observedMax, std::isfinite(v.max) ? v.max : v.value);
        }
        double lo = 0.0, hi = nominalTop;
        if (kind_ == Kind::Yalk && std::isfinite(yalk_.pointV)) {
            const double center = std::isfinite(yalk_.actualReferenceV7) ? yalk_.actualReferenceV7 : yalk_.pointV;
            const double span = std::max(.06, std::isfinite(observedMax) && std::isfinite(observedMin)
                                               ? (observedMax - observedMin) * 2.0 : .12);
            lo = std::max(-.1, center - span);
            hi = center + span;
            if (!(hi > lo)) { lo = 0; hi = nominalTop; }
        }
        if (kind_ != Kind::Yalk && std::isfinite(observedMax)) hi = std::max(hi, observedMax * 1.05);
        auto yFor = [&](double value) {
            return area.bottom() - std::clamp((value - lo) / std::max(1e-12, hi - lo), 0.0, 1.0) * area.height();
        };

        p.setFont(QFont(QStringLiteral("Segoe UI"), 7));
        for (int tick = 0; tick <= 4; ++tick) {
            const double value = lo + (hi - lo) * tick / 4.0;
            const double y = yFor(value);
            p.setPen(palette::muted);
            p.drawText(QRectF(0, y - 8, 48, 16), Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(value, 'f', unit == QStringLiteral("Ом") ? 1 : 3));
        }

        const double cell = area.width() / std::max(1, values.size());
        int rendered = 0;
        for (int i = 0; i < values.size(); ++i) {
            const auto& v = values[i];
            const double x = area.left() + i * cell;
            const QRectF hit(x, area.top(), cell, area.height() + 24);
            if (std::isfinite(v.value)) {
                ++rendered;
                const QColor color = v.warning ? palette::amber
                    : v.state == VerificationState::NeNorma || v.state == VerificationState::Error
                        ? palette::red : palette::cyan;
                const double top = yFor(v.value);
                p.fillRect(QRectF(x + cell * .20, top, std::max(2.0, cell * .60), area.bottom() - top), color);
                if (std::isfinite(v.min) && std::isfinite(v.max)) {
                    p.setPen(QPen(palette::text, 1));
                    p.drawLine(QPointF(x + cell / 2, yFor(v.min)), QPointF(x + cell / 2, yFor(v.max)));
                }
                hits_.push_back({hit,
                    QStringLiteral("Канал %1\n%2 %3\nmin…max %4…%5")
                        .arg(v.key).arg(v.value, 0, 'f', unit == QStringLiteral("Ом") ? 2 : 4).arg(unit)
                        .arg(v.min, 0, 'f', unit == QStringLiteral("Ом") ? 2 : 4)
                        .arg(v.max, 0, 'f', unit == QStringLiteral("Ом") ? 2 : 4)});
            }
            if (i == 0 || i == values.size() - 1 || (i + 1) % (values.size() > 40 ? 5 : 2) == 0) {
                p.setPen(palette::muted);
                p.setFont(QFont(QStringLiteral("Segoe UI"), 7));
                p.drawText(QRectF(x - cell, area.bottom() + 4, cell * 3, 15), Qt::AlignCenter, QString::number(v.key));
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
    Kind kind_;
    YalkAnalogFrame yalk_;
    YtpFrame ytp_;
    QVector<double> passive_;
    QVector<QPair<QRectF, QString>> hits_;
};

class ContactPlane final : public QWidget
{
public:
    explicit ContactPlane(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("yalkContactThresholdOverview"));
        setMinimumHeight(360);
    }
    void setFrame(const YalkContactFrame& frame, int totalMeasurements)
    {
        frame_ = frame;
        setProperty("contactMeasurementCount", totalMeasurements);
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette::panel3);
        const QRectF analog(54, 28, width() - 68, (height() - 72) * .70);
        const QRectF digital(54, analog.bottom() + 10, width() - 68, (height() - 72) * .30 - 10);
        p.setPen(QPen(palette::line, 1)); p.drawRect(analog); p.drawRect(digital);
        const double maxV = 2.7;
        const double cell = analog.width() / std::max(1, frame_.channels.size());
        for (int i = 0; i < frame_.channels.size(); ++i) {
            const auto& c = frame_.channels[i];
            const double x = analog.left() + i * cell;
            if (std::isfinite(c.currentV)) {
                const double top = analog.bottom() - std::clamp(c.currentV / maxV, 0.0, 1.0) * analog.height();
                const QColor color = c.verification == VerificationState::NeNorma ? palette::red : palette::cyan;
                p.fillRect(QRectF(x + cell*.2, top, std::max(2.0, cell*.6), analog.bottom()-top), color);
            }
            if (c.logic >= 0) {
                p.setPen(c.logic ? palette::green : palette::muted);
                p.setFont(QFont(QStringLiteral("Segoe UI"), 7, QFont::DemiBold));
                p.drawText(QRectF(x, digital.top(), cell, digital.height()), Qt::AlignCenter, QString::number(c.logic));
            }
            if (i == 0 || i == frame_.channels.size()-1 || (i+1)%5==0) {
                p.setPen(palette::muted); p.setFont(QFont(QStringLiteral("Segoe UI"), 7));
                p.drawText(QRectF(x-cell, analog.bottom()+1, cell*3, 14), Qt::AlignCenter, QString::number(c.physicalAddress));
            }
        }
        p.setPen(palette::muted); p.setFont(QFont(QStringLiteral("Segoe UI"), 8));
        p.drawText(QRectF(2, analog.top(), 48, 18), Qt::AlignRight, QStringLiteral("U, В"));
        p.drawText(QRectF(2, digital.top(), 48, 18), Qt::AlignRight, QStringLiteral("D"));
    }
private:
    YalkContactFrame frame_;
};

class OverloadPlane final : public QWidget
{
public:
    explicit OverloadPlane(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("yalkOverloadOverview"));
        setMinimumHeight(400);
        setMouseTracking(true);
    }
    void setFrame(const YalkOverloadFrame& frame)
    {
        frame_ = frame;
        int measured = 0;
        for (const auto& c : frame.channels) if (std::isfinite(c.currentCode)) ++measured;
        setProperty("overloadMeasurementCount", measured);
        update();
    }
protected:
    void mouseMoveEvent(QMouseEvent* event) override
    {
        for (const auto& hit : hits_) if (hit.first.contains(event->position())) {
            QToolTip::showText(event->globalPosition().toPoint(), hit.second, this, hit.first.toRect()); return;
        }
        QToolTip::hideText();
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.fillRect(rect(), palette::panel3); hits_.clear();
        const QRectF area(54, 42, width()-68, height()-82);
        p.setPen(QPen(palette::line,1)); p.drawRect(area);
        double lo=std::numeric_limits<double>::infinity(), hi=-std::numeric_limits<double>::infinity();
        for(const auto& c:frame_.channels){ if(std::isfinite(c.baselineCode)){lo=std::min(lo,c.baselineCode);hi=std::max(hi,c.baselineCode);} if(std::isfinite(c.currentCode)){lo=std::min(lo,c.currentCode);hi=std::max(hi,c.currentCode);} }
        if(!std::isfinite(lo)||!std::isfinite(hi)){ p.setPen(palette::dim);p.drawText(area,Qt::AlignCenter,QStringLiteral("ожидание baseline / current"));return; }
        double pad=std::max(1.0,(hi-lo)*.16);lo-=pad;hi+=pad;if(!(hi>lo))hi=lo+1;
        auto y=[&](double v){return area.bottom()-(v-lo)/(hi-lo)*area.height();};
        for(int tick=0;tick<=4;++tick){double yy=area.top()+tick*area.height()/4.0;p.setPen(QPen(palette::lineSoft,1,Qt::DashLine));p.drawLine(QPointF(area.left(),yy),QPointF(area.right(),yy));}
        const double cell=area.width()/88.0;
        double maxDelta=0.0;
        for(int i=0;i<frame_.channels.size()&&i<88;++i){const auto&c=frame_.channels[i];const double x=area.left()+i*cell;if(c.physicalAddress==frame_.stressedChannel){p.fillRect(QRectF(x,area.top(),cell,area.height()),QColor(palette::blue.red(),palette::blue.green(),palette::blue.blue(),45));}
            if(std::isfinite(c.baselineCode)){const double top=y(c.baselineCode);p.fillRect(QRectF(x+cell*.08,top,std::max(2.0,cell*.84),area.bottom()-top),QColor(142,166,183,70));}
            if(std::isfinite(c.currentCode)){maxDelta=std::max(maxDelta,std::abs(c.deltaCode));const double top=y(c.currentCode);const QColor color=(c.verification==VerificationState::NeNorma||c.verification==VerificationState::Error)?palette::red:palette::cyan;p.fillRect(QRectF(x+cell*.28,top,std::max(2.0,cell*.44),area.bottom()-top),color);hits_.push_back({QRectF(x,area.top(),cell,area.height()),QStringLiteral("Канал %1\nbaseline %2\ncurrent %3\nΔcode %4\n%5").arg(c.physicalAddress).arg(c.baselineCode,0,'f',1).arg(c.currentCode,0,'f',1).arg(c.deltaCode,0,'f',1).arg(verificationText(c.verification))});}
            if(i==0||i==87||(i+1)%5==0){p.setPen(palette::muted);p.setFont(QFont(QStringLiteral("Segoe UI"),7));p.drawText(QRectF(x-cell,area.bottom()+3,cell*3,14),Qt::AlignCenter,QString::number(c.physicalAddress));}}
        p.setPen(palette::muted);p.setFont(QFont(QStringLiteral("Segoe UI"),8));p.drawText(QRectF(area.left(),8,area.width(),20),Qt::AlignLeft,QStringLiteral("baseline + current · критерий |current − baseline| ≤ %1 кода").arg(frame_.criterionCode,0,'f',0));
        p.setPen(maxDelta>frame_.criterionCode?palette::red:palette::blue2);p.drawText(QRectF(area.left(),8,area.width(),20),Qt::AlignRight,QStringLiteral("%1 · канал %2 · воздействие %3/%4 · max |Δ| %5").arg(frame_.polarity).arg(frame_.stressedChannel).arg(frame_.impactIndex).arg(frame_.impactCount).arg(maxDelta,0,'f',1));
    }
private:
    YalkOverloadFrame frame_;
    QVector<QPair<QRectF,QString>> hits_;
};

class YvpPlane final : public QWidget
{
public:
    explicit YvpPlane(QWidget* parent=nullptr):QWidget(parent)
    {
        setObjectName(QStringLiteral("yvpEightChannelOverview"));
        setMinimumHeight(360);
    }
    void setFrame(const YvpFrame& frame, int completedPoints)
    {
        frame_=frame;
        int rendered=0;for(const auto&c:frame.channels)if(std::isfinite(c.measuredValue))++rendered;
        setProperty("yvpRenderedChannelCount",rendered);
        setProperty("yvpCompletedPointCount",completedPoints);
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),palette::panel3);
        const QRectF area(48,42,width()-62,height()-80);p.setPen(QPen(palette::line,1));p.drawRect(area);
        double hi=1.0;for(const auto&c:frame_.channels){if(std::isfinite(c.stimulusValue))hi=std::max(hi,c.stimulusValue);if(std::isfinite(c.calculatedGain))hi=std::max(hi,c.calculatedGain);}hi*=1.15;
        const double cell=area.width()/8.0;
        for(int i=0;i<8;++i){const auto&c=frame_.channels[i];const double x=area.left()+i*cell;if(c.channel==frame_.testedChannel)p.fillRect(QRectF(x,area.top(),cell,area.height()),QColor(palette::blue.red(),palette::blue.green(),palette::blue.blue(),35));
            if(std::isfinite(c.stimulusValue)){const double h=std::clamp(c.stimulusValue/hi,0.0,1.0)*area.height();p.fillRect(QRectF(x+cell*.16,area.bottom()-h,cell*.25,h),QColor(palette::blue2.red(),palette::blue2.green(),palette::blue2.blue(),130));}
            if(std::isfinite(c.calculatedGain)){const double h=std::clamp(c.calculatedGain/hi,0.0,1.0)*area.height();const QColor color=c.verification==VerificationState::NeNorma?palette::red:palette::cyan;p.fillRect(QRectF(x+cell*.55,area.bottom()-h,cell*.25,h),color);}
            p.setPen(palette::muted);p.setFont(QFont(QStringLiteral("Segoe UI"),8));p.drawText(QRectF(x,area.bottom()+4,cell,16),Qt::AlignCenter,QString::number(i+1));}
        p.setPen(palette::muted);p.setFont(QFont(QStringLiteral("Segoe UI"),8));p.drawText(QRectF(area.left(),8,area.width(),20),Qt::AlignLeft,QStringLiteral("подано / измерено · 8 каналов"));p.drawText(QRectF(area.left(),8,area.width(),20),Qt::AlignRight,QStringLiteral("Kу %1 · %2 Гц · выполнено %3/%4").arg(frame_.gain,0,'g',6).arg(frame_.frequencyHz,0,'g',8).arg(property("yvpCompletedPointCount").toInt()).arg(frame_.pointCount));
    }
private:YvpFrame frame_;
};

} // namespace ubsi::ui
