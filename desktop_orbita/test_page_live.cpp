// Targeted visualization layer for the existing TestPage.
//
// The production screen layout and workflow stay in test_page.cpp.  This
// translation unit compiles that implementation and replaces only Plot's paint
// handling through an event filter.  We keep this isolated because production
// explicitly asked to preserve the familiar screen and improve only the
// uninformative "Все каналы" overview.

#include "test_page.h"
#include "equipment_control_widget.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QEvent>
#include <QFrame>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>

#include <functional>
#include <iterator>
#include <stdexcept>
#include <utility>

// test_page.cpp keeps Plot local to its translation unit.  Include the existing
// implementation here so the visualization adapter can read the already stored
// samples without duplicating the TestPage logic or changing RunStore data.
// All headers above are included before this macro, so it only exposes Plot's
// own state in this translation unit; Qt/STL class definitions are unaffected.
#define private public
#include "test_page.cpp"
#undef private

namespace {

struct ChannelHitRegion {
    QRectF rect;
    QString channel;
    QString point;
};

QString sampleUnit(const Plot* plot, const Plot::Sample& sample)
{
    if (sample.point.contains(QStringLiteral("Ом"))) return QStringLiteral("Ом");
    if (plot->axisText_.contains(QStringLiteral("ЯТП"))
        && !plot->axisText_.contains(QStringLiteral("ЯЛК"))) return QStringLiteral("Ом");
    return QStringLiteral("В");
}

bool usesCombinedPercentScale(const Plot* plot)
{
    return plot->axisText_.contains(QStringLiteral("общей приведённой шкале"));
}

double nativeValue(const Plot* plot, const Plot::Sample& sample, double value)
{
    if (!usesCombinedPercentScale(plot)) return value;
    return sampleUnit(plot, sample) == QStringLiteral("Ом")
        ? value * 240.0 / 100.0
        : value * 6.2 / 100.0;
}

class LiveChannelOverviewFilter final : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        auto* plot = dynamic_cast<Plot*>(watched);
        if (!plot) return QObject::eventFilter(watched, event);

        if (event->type() == QEvent::Show) {
            plot->setMinimumHeight(qMax(plot->minimumHeight(), 360));
            return QObject::eventFilter(watched, event);
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const auto regions = hitRegions_.value(plot);
            for (const auto& hit : regions) {
                if (!hit.rect.contains(mouse->pos())) continue;
                if (!hit.channel.isEmpty()) plot->selectedChannel_ = hit.channel;
                if (!hit.point.isEmpty()) plot->selectedPoint_ = hit.point;
                plot->update();
                mouse->accept();
                return true;
            }
            return QObject::eventFilter(watched, event);
        }
        if (event->type() != QEvent::Paint)
            return QObject::eventFilter(watched, event);

        paintPlot(plot);
        return true;
    }

private:
    void paintPlot(Plot* plot)
    {
        QPainter painter(plot);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(plot->rect(), QColor("#0e1115"));
        hitRegions_[plot].clear();

        if (plot->points_.isEmpty()) {
            painter.setPen(QColor("#7e8a98"));
            painter.drawText(plot->rect(), Qt::AlignCenter,
                QStringLiteral("Во время проверки появятся:\n"
                               "выбранный канал подробно и свежие отсчёты всех каналов"));
            return;
        }

        const double detailHeight = qMax(115.0, plot->height() * 0.38);
        const QRectF detail(50.0, 28.0, qMax(100, plot->width() - 70), detailHeight);
        const QRectF allChannels(50.0, detail.bottom() + 43.0,
            qMax(100, plot->width() - 70),
            qMax(95.0, plot->height() - detail.bottom() - 53.0));

        painter.setPen(QColor("#2c333d"));
        painter.drawRect(detail);

        QString currentUnit;
        for (auto it = plot->points_.crbegin(); it != plot->points_.crend(); ++it) {
            if (it->point == plot->selectedPoint_) {
                currentUnit = sampleUnit(plot, *it);
                break;
            }
        }
        if (currentUnit.isEmpty()) currentUnit = sampleUnit(plot, plot->points_.back());

        QVector<const Plot::Sample*> selected;
        for (const auto& item : plot->points_) {
            if (item.channel == plot->selectedChannel_ && sampleUnit(plot, item) == currentUnit)
                selected.push_back(&item);
        }
        if (selected.isEmpty()) selected.push_back(&plot->points_.back());

        QVector<double> referenceTrace;
        QVector<double> measuredTrace;
        QVector<int> boundaries;
        for (const auto* item : selected) {
            const int count = qMax(1, item->samples.size());
            for (int index = 0; index < count; ++index) {
                referenceTrace.push_back(item->reference);
                measuredTrace.push_back(item->samples.isEmpty() ? item->measured : item->samples[index]);
            }
            boundaries.push_back(measuredTrace.size());
        }

        const auto detailY = [&](double value) {
            const double span = qMax(0.000001, plot->maximum_ - plot->minimum_);
            return detail.bottom()
                - qBound(0.0, (value - plot->minimum_) / span, 1.0) * detail.height();
        };
        const auto drawDetailTrace = [&](const QVector<double>& values,
                                         const QColor& color, Qt::PenStyle style) {
            if (values.isEmpty()) return;
            QPainterPath path;
            for (int index = 0; index < values.size(); ++index) {
                const double x = detail.left() + (values.size() == 1
                    ? detail.width() / 2.0
                    : detail.width() * index / static_cast<double>(values.size() - 1));
                const double y = detailY(values[index]);
                if (index == 0) path.moveTo(x, y); else path.lineTo(x, y);
            }
            painter.setPen(QPen(color, 2.0, style));
            painter.drawPath(path);
        };
        drawDetailTrace(referenceTrace, QColor("#55b7ff"), Qt::DashLine);
        drawDetailTrace(measuredTrace, QColor("#70d79b"), Qt::SolidLine);

        painter.setPen(QColor("#e6eaf0"));
        painter.drawText(QRectF(detail.left(), 4.0, detail.width(), 20.0), Qt::AlignLeft,
            QStringLiteral("Канал %1 · подробно · свежие отсчёты по выполненным точкам")
                .arg(plot->selectedChannel_.isEmpty() ? QStringLiteral("—")
                                                       : plot->selectedChannel_));
        painter.setPen(QColor("#8b95a3"));
        painter.drawText(5, static_cast<int>(detail.top()) + 5, plot->maximumLabel_);
        painter.drawText(12, static_cast<int>(detail.bottom()), plot->minimumLabel_);

        int previous = 0;
        for (int index = 0; index < selected.size(); ++index) {
            const int end = boundaries.value(index);
            const double centerIndex = (previous + end - 1) / 2.0;
            const double x = detail.left() + (referenceTrace.size() <= 1
                ? detail.width() / 2.0
                : detail.width() * centerIndex / (referenceTrace.size() - 1));
            painter.drawText(QRectF(x - 48.0, detail.bottom() + 3.0, 96.0, 18.0),
                             Qt::AlignCenter, selected[index]->point);
            previous = end;
        }
        painter.setPen(QColor("#55b7ff"));
        painter.drawText(static_cast<int>(detail.right()) - 190, 20, plot->referenceLegend_);
        painter.setPen(QColor("#70d79b"));
        painter.drawText(static_cast<int>(detail.right()) - 92, 20, plot->measuredLegend_);

        QVector<const Plot::Sample*> visible;
        for (const auto& item : plot->points_) {
            if (item.point == plot->selectedPoint_ && sampleUnit(plot, item) == currentUnit)
                visible.push_back(&item);
        }
        if (visible.isEmpty()) {
            for (const auto& item : plot->points_)
                if (sampleUnit(plot, item) == currentUnit) visible.push_back(&item);
        }

        painter.setPen(QColor("#e6eaf0"));
        painter.drawText(QRectF(allChannels.left(), detail.bottom() + 23.0,
                                allChannels.width(), 18.0),
            Qt::AlignLeft,
            QStringLiteral("Все каналы · точка %1 · свежие отсчёты, %2 · показано %3")
                .arg(plot->selectedPoint_.isEmpty() ? QStringLiteral("—")
                                                    : plot->selectedPoint_)
                .arg(currentUnit)
                .arg(visible.size()));
        painter.setPen(QColor("#8b95a3"));
        painter.drawText(QRectF(allChannels.right() - 360.0, detail.bottom() + 23.0,
                                360.0, 18.0),
            Qt::AlignRight, QStringLiteral("клик по каналу → подробный график сверху"));

        if (visible.isEmpty()) return;

        double commonMin = 0.0;
        double commonMax = 0.0;
        bool haveRange = false;
        const auto consume = [&](double value) {
            if (!haveRange) {
                commonMin = commonMax = value;
                haveRange = true;
            } else {
                commonMin = qMin(commonMin, value);
                commonMax = qMax(commonMax, value);
            }
        };
        for (const auto* item : visible) {
            if (item->samples.isEmpty()) consume(nativeValue(plot, *item, item->measured));
            else for (const double value : item->samples) consume(nativeValue(plot, *item, value));
        }
        if (!haveRange) { commonMin = 0.0; commonMax = 1.0; }
        double commonSpan = commonMax - commonMin;
        const double minimumSpan = currentUnit == QStringLiteral("В") ? 0.004 : 0.2;
        if (commonSpan < minimumSpan) {
            const double center = (commonMin + commonMax) / 2.0;
            commonMin = center - minimumSpan / 2.0;
            commonMax = center + minimumSpan / 2.0;
            commonSpan = minimumSpan;
        }
        const double padding = commonSpan * 0.12;
        commonMin -= padding;
        commonMax += padding;
        commonSpan = commonMax - commonMin;

        const int count = visible.size();
        const int columns = qBound(8, static_cast<int>(allChannels.width() / 66.0), 16);
        const int rows = qMax(1, (count + columns - 1) / columns);
        const double gap = 3.0;
        const double cellWidth = (allChannels.width() - gap * (columns - 1)) / columns;
        const double cellHeight = qMax(24.0,
            (allChannels.height() - gap * (rows - 1)) / rows);

        QFont smallFont = painter.font();
        smallFont.setPointSizeF(qMax(6.5, smallFont.pointSizeF() - 2.0));
        QFont valueFont = smallFont;
        valueFont.setBold(true);

        for (int index = 0; index < count; ++index) {
            const auto* item = visible[index];
            const int row = index / columns;
            const int column = index % columns;
            const QRectF card(allChannels.left() + column * (cellWidth + gap),
                              allChannels.top() + row * (cellHeight + gap),
                              cellWidth, cellHeight);
            const bool selected = item->channel == plot->selectedChannel_;
            const QColor stateColor = item->passed ? QColor("#42c88a") : QColor("#e25f5f");
            const QColor border = selected ? QColor("#55b7ff")
                : (item->passed ? QColor("#2c4f42") : QColor("#8a3a3a"));

            painter.setBrush(QColor(selected ? "#142536" : "#11161c"));
            painter.setPen(QPen(border, selected ? 1.8 : 1.0));
            painter.drawRoundedRect(card, 3.0, 3.0);

            painter.setFont(smallFont);
            painter.setPen(selected ? QColor("#8dceff") : QColor("#9aa6b4"));
            painter.drawText(card.adjusted(5.0, 2.0, -3.0, 0.0),
                             Qt::AlignTop | Qt::AlignLeft,
                             QStringLiteral("К%1").arg(item->channel));

            const double lastScaled = item->samples.isEmpty()
                ? item->measured : item->samples.back();
            const double lastValue = nativeValue(plot, *item, lastScaled);
            painter.setFont(valueFont);
            painter.setPen(QColor("#e6eaf0"));
            const int decimals = currentUnit == QStringLiteral("В") ? 3 : 2;
            painter.drawText(card.adjusted(25.0, 2.0, -4.0, 0.0),
                             Qt::AlignTop | Qt::AlignRight,
                             QString::number(lastValue, 'f', decimals));

            const QRectF spark = card.adjusted(4.0, 14.0, -4.0, -4.0);
            const double nativeReference = nativeValue(plot, *item, item->reference);
            const double referenceY = spark.bottom()
                - qBound(0.0, (nativeReference - commonMin) / commonSpan, 1.0)
                    * spark.height();
            painter.setPen(QPen(QColor("#26313b"), 1.0));
            painter.drawLine(QPointF(spark.left(), referenceY),
                             QPointF(spark.right(), referenceY));

            if (item->samples.size() >= 2) {
                QPainterPath path;
                for (int sampleIndex = 0; sampleIndex < item->samples.size(); ++sampleIndex) {
                    const double x = spark.left() + spark.width() * sampleIndex
                        / static_cast<double>(item->samples.size() - 1);
                    const double value = nativeValue(plot, *item, item->samples[sampleIndex]);
                    const double y = spark.bottom()
                        - qBound(0.0, (value - commonMin) / commonSpan, 1.0) * spark.height();
                    if (sampleIndex == 0) path.moveTo(x, y); else path.lineTo(x, y);
                }
                painter.setPen(QPen(stateColor, selected ? 1.7 : 1.2));
                painter.drawPath(path);
            } else {
                const double y = spark.bottom()
                    - qBound(0.0, (lastValue - commonMin) / commonSpan, 1.0) * spark.height();
                painter.setPen(QPen(stateColor, 1.5));
                painter.drawLine(QPointF(spark.left(), y), QPointF(spark.right(), y));
            }

            hitRegions_[plot].push_back(ChannelHitRegion{card, item->channel, item->point});
        }
    }

    QHash<Plot*, QVector<ChannelHitRegion>> hitRegions_;
};

void installLiveChannelOverview()
{
    if (auto* application = QCoreApplication::instance())
        application->installEventFilter(new LiveChannelOverviewFilter(application));
}

Q_COREAPP_STARTUP_FUNCTION(installLiveChannelOverview)

} // namespace
