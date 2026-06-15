#ifndef PLOT_WIDGET_H
#define PLOT_WIDGET_H

#include <QWidget>
#include <qcustomplot.h>
#include <string>
#include <vector>

class PlotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PlotWidget(QWidget* parent = nullptr);

    // Добавить одну точку времени для всех каналов (time-series режим)
    void addSamples(double timeSeconds,
                    const std::vector<double>& values,
                    const std::vector<std::string>& names);

    // Установить ширину скользящего окна (по умолчанию 30 с)
    void setWindowSeconds(double seconds);

    // Очистить все графики и сбросить состояние
    void clearAll();

private:
    void ensureGraphs(int count, const std::vector<std::string>& names);

    QCustomPlot* plot_;
    double windowSeconds_ = 30.0;
    std::vector<std::string> lastNames_;

    static constexpr int MAX_GRAPHS = 32;
    static const QColor COLORS[16];
};

#endif // PLOT_WIDGET_H
