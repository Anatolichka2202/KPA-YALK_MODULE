#include "plot_widget.h"
#include <QVBoxLayout>
#include <algorithm>

const QColor PlotWidget::COLORS[16] = {
    {0x1f, 0x77, 0xb4}, {0xff, 0x7f, 0x0e}, {0x2c, 0xa0, 0x2c}, {0xd6, 0x27, 0x28},
    {0x94, 0x67, 0xbd}, {0x8c, 0x56, 0x4b}, {0xe3, 0x77, 0xc2}, {0x7f, 0x7f, 0x7f},
    {0xbc, 0xbd, 0x22}, {0x17, 0xbe, 0xcf}, {0xae, 0xc7, 0xe8}, {0xff, 0xbb, 0x78},
    {0x98, 0xdf, 0x8a}, {0xff, 0x98, 0x96}, {0xc5, 0xb0, 0xd5}, {0xc4, 0x9c, 0x94}
};

PlotWidget::PlotWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    plot_ = new QCustomPlot(this);
    layout->addWidget(plot_);

    plot_->xAxis->setLabel("Время, с");
    plot_->yAxis->setLabel("Код АЦП");
    plot_->legend->setVisible(true);
    plot_->legend->setFont(QFont("Segoe UI", 8));
    plot_->legend->setBrush(QBrush(QColor(255, 255, 255, 180)));
    plot_->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectLegend);

    plot_->xAxis->grid()->setSubGridVisible(true);
    plot_->yAxis->grid()->setSubGridVisible(true);
}

void PlotWidget::ensureGraphs(int count, const std::vector<std::string>& names)
{
    while (plot_->graphCount() < count && plot_->graphCount() < MAX_GRAPHS) {
        int idx = plot_->graphCount();
        QCPGraph* g = plot_->addGraph();
        QPen pen(COLORS[idx % 16]);
        pen.setWidthF(1.5);
        g->setPen(pen);
        g->setLineStyle(QCPGraph::lsLine);
        if (idx < (int)names.size())
            g->setName(QString::fromStdString(names[idx]));
    }

    if (names != lastNames_) {
        for (int i = 0; i < plot_->graphCount() && i < (int)names.size(); ++i)
            plot_->graph(i)->setName(QString::fromStdString(names[i]));
        lastNames_ = names;
    }
}

void PlotWidget::addSamples(double timeSeconds,
                             const std::vector<double>& values,
                             const std::vector<std::string>& names)
{
    if (values.empty()) return;

    int count = std::min((int)values.size(), MAX_GRAPHS);
    ensureGraphs(count, names);

    double tMin = timeSeconds - windowSeconds_;

    for (int i = 0; i < count; ++i) {
        auto* g = plot_->graph(i);
        g->addData(timeSeconds, values[i]);
        g->data()->removeBefore(tMin);
    }

    plot_->xAxis->setRange(tMin, timeSeconds);

    // Авто-масштаб Y по видимым данным
    bool found = false;
    double yMin = 0, yMax = 1;
    for (int i = 0; i < count; ++i) {
        bool f = false;
        QCPRange r = plot_->graph(i)->getValueRange(f, QCP::sdBoth,
                                                     QCPRange(tMin, timeSeconds));
        if (f) {
            if (!found) { yMin = r.lower; yMax = r.upper; found = true; }
            else { yMin = std::min(yMin, r.lower); yMax = std::max(yMax, r.upper); }
        }
    }
    if (found) {
        double margin = (yMax - yMin) * 0.05;
        if (margin < 1.0) margin = 1.0;
        plot_->yAxis->setRange(yMin - margin, yMax + margin);
    }

    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void PlotWidget::setWindowSeconds(double seconds)
{
    windowSeconds_ = seconds;
}

void PlotWidget::clearAll()
{
    plot_->clearGraphs();
    lastNames_.clear();
    plot_->replot();
}
