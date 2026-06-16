#include "plot_widget.h"
#include <QVBoxLayout>
#include <algorithm>

PlotWidget::PlotWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    plot_ = new QCustomPlot(this);
    layout->addWidget(plot_);

    plot_->xAxis->setLabel("Канал");
    plot_->yAxis->setLabel("Код");
    plot_->yAxis->setRange(0, 1023);
    plot_->xAxis->setTickLabelRotation(60);
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

    bars_ = new QCPBars(plot_->xAxis, plot_->yAxis);
    bars_->setPen(QPen(QColor(0x1f, 0x77, 0xb4)));
    bars_->setBrush(QBrush(QColor(0x1f, 0x77, 0xb4)));
    bars_->setWidthType(QCPBars::wtPlotCoords);
    bars_->setWidth(0.7);

    ticker_ = new QCPAxisTickerText;
    plot_->xAxis->setTicker(QSharedPointer<QCPAxisTickerText>(ticker_));
}

void PlotWidget::setChannelNames(const std::vector<std::string>& names)
{
    QMap<double, QString> tickLabels;
    for (size_t i = 0; i < names.size(); ++i)
        tickLabels[static_cast<double>(i)] = QString::fromStdString(names[i]);
    ticker_->setTicks(tickLabels);

    if (!names.empty())
        plot_->xAxis->setRange(-0.5, names.size() - 0.5);
    plot_->replot();
}

void PlotWidget::setValues(const std::vector<double>& values,
                           const std::vector<std::string>& names)
{
    if (values.empty()) return;

    if (lastNames_ != names) {
        setChannelNames(names);
        lastNames_ = names;
    }

    QVector<double> keys(values.size()), data(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        keys[i] = static_cast<double>(i);
        data[i] = values[i];
    }
    bars_->setData(keys, data);

    // Авто-масштаб Y: не опускаем потолок ниже 1023 (типовой диапазон 10-битного кода)
    double maxVal = *std::max_element(values.begin(), values.end());
    double curMax = plot_->yAxis->range().upper;
    if (maxVal > curMax)
        plot_->yAxis->setRange(0, maxVal * 1.05);
    else if (maxVal < curMax * 0.5 && curMax > 1023.0)
        plot_->yAxis->setRange(0, std::max(1023.0, maxVal * 1.2));

    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void PlotWidget::clearAll()
{
    bars_->data()->clear();
    ticker_->clear();
    lastNames_.clear();
    plot_->yAxis->setRange(0, 1023);
    plot_->replot();
}
