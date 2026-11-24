#pragma once

#include <QPainter>
#include <QPainterPath>
#include <QWidget>

#include <algorithm>
#include <cmath>
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


} // namespace brp::ui
