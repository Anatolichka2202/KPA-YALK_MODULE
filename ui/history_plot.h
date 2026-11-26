#pragma once

#include <QPainter>
#include <QMouseEvent>
#include <QVariantAnimation>
#include <QPainterPath>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace brp::ui {

namespace palette {
inline const QColor panel3{"#0e1e2c"};
inline const QColor line{"#264257"};
inline const QColor lineSoft{"#1a3346"};
inline const QColor text{"#eaf4fb"};
inline const QColor muted{"#8ea6b7"};
inline const QColor dim{"#61788a"};
inline const QColor blue2{"#58a5ff"};
inline const QColor green{"#35cf79"};
inline const QColor red{"#ef5a5a"};
inline const QColor amber{"#e1ad46"};
inline const QColor cyan{"#61d5e8"};
}


class HistoryPlot final : public QWidget
{
public:
    struct NamedSeries {
        QString name;
        QVector<double> values;
    };
    struct BarValue {
        QString name;
        double value = 0.0;
        bool valid = false;
        bool stale = false;
        QString address;
    };
    struct TimedSeries {
        QString name;
        QVector<QPointF> points; // x: monotonic elapsed seconds; y: unmodified decoded M16 code
        double nowSeconds = 0.0;
        double windowSeconds = 15.0;
    };

    explicit HistoryPlot(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(120);
        barsAnimation_.setDuration(55);
        barsAnimation_.setStartValue(0.0);
        barsAnimation_.setEndValue(1.0);
        connect(&barsAnimation_, &QVariantAnimation::valueChanged, this, [this] { update(); });
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
        bars_.clear();
        named_.clear();
        timed_.reset();
        first_ = first;
        second_ = second;
        update();
    }

    void setFixedRange(double minimum, double maximum)
    {
        if (std::isfinite(minimum) && std::isfinite(maximum) && maximum > minimum)
            fixedRange_ = std::make_pair(minimum, maximum);
        else
            fixedRange_.reset();
        update();
    }

    void setNamedSeries(QVector<NamedSeries> series)
    {
        timed_.reset();
        bars_.clear();
        named_ = std::move(series);
        update();
    }

    void setTimedSeries(TimedSeries series)
    {
        first_.clear();
        second_.clear();
        bars_.clear();
        named_.clear();
        timed_ = std::move(series);
        setProperty("brpTrendRawPoints", timed_->points.size());
        update();
    }

    // Static bar view for the most recent M16/M8 snapshot. Unlike a line graph,
    // it does not suggest continuity between independent channel addresses.
    void setBars(QVector<BarValue> bars)
    {
        first_.clear();
        second_.clear();
        named_.clear();
        timed_.reset();
        QVector<double> previous;
        previous.reserve(bars.size());
        for (qsizetype i = 0; i < bars.size(); ++i) {
            const auto& next = bars[i];
            previous.append(i < bars_.size() && bars_[i].address == next.address
                && bars_[i].valid && !bars_[i].stale && next.valid && !next.stale
                ? displayedBar(i) : next.value);
        }
        barsAnimation_.stop();
        barOrigins_ = std::move(previous);
        bars_ = std::move(bars);
        barsAnimation_.start();
        update();
    }

    void onBarSelected(std::function<void(const QString&)> callback)
    {
        barSelected_ = std::move(callback);
    }

    void clear()
    {
        first_.clear();
        second_.clear();
        named_.clear();
        bars_.clear();
        timed_.reset();
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        const auto area = plotArea();
        if (event->button() == Qt::LeftButton && barSelected_ && !bars_.isEmpty()
            && area.contains(event->position())) {
            const auto index = std::min(bars_.size() - 1, static_cast<qsizetype>(
                (event->position().x() - area.left()) * bars_.size() / area.width()));
            const auto address = bars_[index].address;
            if (!address.isEmpty()) {
                barSelected_(address);
                event->accept();
                return;
            }
        }
        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette::panel3);

        p.setPen(palette::text);
        p.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        p.drawText(QRectF(12, 6, width() - 24, 20), Qt::AlignLeft | Qt::AlignVCenter, title_);

        const auto area = plotArea();
        p.setPen(QPen(palette::line, 1));
        p.drawRect(area);
        for (int tick = 1; tick < 4; ++tick) {
            const double y = area.top() + tick * area.height() / 4.0;
            p.setPen(QPen(palette::lineSoft, 1, Qt::DashLine));
            p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }

        if (timed_) {
            const auto& trend = *timed_;
            const double window = std::max(0.25, trend.windowSeconds);
            const double right = std::max(window, trend.nowSeconds);
            const double left = right - window;
            bool any = false;
            double low = 0.0, high = 0.0;
            for (const auto& point : trend.points) {
                if (point.x() < left || !std::isfinite(point.y())) continue;
                if (!any) { low = high = point.y(); any = true; }
                else { low = std::min(low, point.y()); high = std::max(high, point.y()); }
            }
            if (!any) {
                p.setPen(palette::dim);
                p.drawText(area, Qt::AlignCenter, QStringLiteral("Нет подтверждённых отсчётов"));
                return;
            }
            // Auto-scale to the real local amplitude: never filter or smooth samples.
            const double centre = (low + high) / 2.0;
            const double margin = std::max(2.0, (high - low) * 0.15);
            if (high - low < 2.0) { low = centre - margin; high = centre + margin; }
            else { low -= margin; high += margin; }
            const auto ordinate = [&](double y) {
                return area.bottom() - (y - low) * area.height() / (high - low);
            };
            p.setFont(QFont(QStringLiteral("Segoe UI"), 8));
            for (int tick = 0; tick <= 4; ++tick) {
                const double value = low + (high - low) * tick / 4.0;
                const double y = ordinate(value);
                p.setPen(palette::muted);
                p.drawText(QRectF(0, y - 9, 46, 18), Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(value, 'f', 1));
                const double second = left + window * tick / 4.0;
                const double x = area.left() + area.width() * tick / 4.0;
                p.setPen(palette::lineSoft);
                p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
                p.setPen(palette::muted);
                p.drawText(QRectF(x - 25, area.bottom() + 1, 50, 16),
                           Qt::AlignCenter, QString::number(second, 'f', 1) + QStringLiteral(" c"));
            }
            QPainterPath curve;
            bool started = false;
            double previous = 0.0;
            for (const auto& point : trend.points) {
                if (point.x() < left || point.x() > right) continue;
                if (!std::isfinite(point.y())) { started = false; continue; }
                const double x = area.left() + (point.x() - left) * area.width() / window;
                const QPointF mapped(x, ordinate(point.y()));
                // A missing block is a gap, not a zero or an interpolated signal.
                if (!started || point.x() - previous > 0.5) curve.moveTo(mapped);
                else curve.lineTo(mapped);
                previous = point.x();
                started = true;
            }
            p.save();
            p.setClipRect(area.adjusted(1, 1, -1, -1));
            p.setPen(QPen(palette::cyan, 1.6));
            p.drawPath(curve);
            p.restore();
            p.setPen(palette::muted);
            p.drawText(QRectF(area.right() - 195, 6, 185, 20), Qt::AlignRight,
                       QStringLiteral("%1 · %2 точек")
                         .arg(trend.name).arg(trend.points.size()));
            return;
        }

        if (!bars_.isEmpty()) {
            // Zero-based scale, with an independent column for each channel.
            const double maximum = [&] {
                double value = 1.0;
                for (const auto& bar : bars_)
                    if (bar.valid && std::isfinite(bar.value))
                        value = std::max(value, bar.value);
                return std::max(1023.0, value);
            }();
            const QRectF plot(area.left(), area.top() + 4, area.width(),
                              std::max(20.0, area.height() - 29));
            p.setFont(QFont(QStringLiteral("Segoe UI"), 8));
            for (int tick = 0; tick <= 4; ++tick) {
                const double value = maximum * tick / 4.0;
                const double y = plot.bottom() - plot.height() * tick / 4.0;
                p.setPen(palette::lineSoft);
                p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
                p.setPen(palette::muted);
                p.drawText(QRectF(0, y - 8, 46, 16), Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(value, 'f', 0));
            }
            const double slot = plot.width() / bars_.size();
            for (int i = 0; i < bars_.size(); ++i) {
                const auto& bar = bars_[i];
                const bool valid = bar.valid && std::isfinite(bar.value);
                const QColor color = valid
                    ? (bar.stale ? palette::dim : QColor::fromHsv((205 + 53 * (i % 6)) % 360, 145, 247))
                    : palette::dim;
                const double ratio = valid ? std::clamp(displayedBar(i) / maximum, 0.0, 1.0) : 0.0;
                const double width = std::max(2.0, std::min(slot * .64, 100.0));
                const double x = plot.left() + (i + .5) * slot - width / 2.0;
                const double height = ratio * plot.height();
                p.fillRect(QRectF(x, plot.bottom() - height, width, height), color);
                p.setPen(valid ? palette::text : palette::dim);
                const QRectF valueRect(plot.left() + i * slot, plot.bottom() - height - 21,
                                       slot, 18);
                p.drawText(valueRect, Qt::AlignCenter,
                           valid ? QString::number(bar.value, 'f', 0) : QStringLiteral("—"));
                const QString label = p.fontMetrics().elidedText(bar.name, Qt::ElideRight,
                                                 std::max(20, int(slot - 6)));
                p.drawText(QRectF(plot.left() + i * slot, plot.bottom() + 5, slot, 20),
                           Qt::AlignCenter, label);
            }
            return;
        }

        QVector<double> finite;
        for (double value : first_) if (std::isfinite(value)) finite.push_back(value);
        for (double value : second_) if (std::isfinite(value)) finite.push_back(value);
        for (const auto& series : named_)
            for (double value : series.values) if (std::isfinite(value)) finite.push_back(value);
        if (finite.isEmpty()) {
            p.setPen(palette::dim);
            p.setFont(QFont(QStringLiteral("Segoe UI"), 9));
            p.drawText(area, Qt::AlignCenter, QStringLiteral("ожидание измерения"));
            return;
        }

        const auto mm = std::minmax_element(finite.begin(), finite.end());
        double lo = *mm.first;
        double hi = *mm.second;
        if (fixedRange_) {
            lo = fixedRange_->first;
            hi = fixedRange_->second;
        } else {
            const double pad = std::max((hi - lo) * .15, std::max(.001, std::abs(hi) * .01));
            lo -= pad;
            hi += pad;
        }
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

        auto count = std::max(first_.size(), second_.size());
        for (const auto& series : named_) count = std::max(count, series.values.size());
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
        int legendRight = width() - 12;
        p.setFont(QFont(QStringLiteral("Segoe UI"), 8));
        for (int i = 0; i < named_.size(); ++i) {
            const auto color = QColor::fromHsv((207 + 137 * (i % 360)) % 360, 135, 247);
            drawSeries(named_[i].values, color);
            if (legendRight < width() / 2) continue;
            const auto name = p.fontMetrics().elidedText(named_[i].name, Qt::ElideRight, 110);
            const int width = p.fontMetrics().horizontalAdvance(name) + 10;
            legendRight -= width;
            p.setPen(color);
            p.drawText(QRectF(legendRight, 6, width, 18), Qt::AlignRight | Qt::AlignVCenter, name);
        }

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
    double displayedBar(qsizetype index) const
    {
        const double target = bars_[index].value;
        if (barsAnimation_.state() != QAbstractAnimation::Running || index >= barOrigins_.size())
            return target;
        return barOrigins_[index] + (target - barOrigins_[index]) * barsAnimation_.currentValue().toDouble();
    }

    QVariantAnimation barsAnimation_;
    QVector<double> barOrigins_;
    QRectF plotArea() const
    {
        return {52.0, 32.0, static_cast<double>(std::max(20, width() - 66)),
                static_cast<double>(std::max(30, height() - 48))};
    }

    std::function<void(const QString&)> barSelected_;
    QString title_, unit_, firstName_, secondName_;
    QVector<double> first_, second_;
    QVector<NamedSeries> named_;
    QVector<BarValue> bars_;
    std::optional<std::pair<double, double>> fixedRange_;
    std::optional<TimedSeries> timed_;
};


} // namespace brp::ui
