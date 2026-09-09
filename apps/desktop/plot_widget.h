#ifndef PLOT_WIDGET_H
#define PLOT_WIDGET_H

#include <QWidget>
#include <qcustomplot.h>
#include <string>
#include <vector>

// Столбчатая диаграмма текущих значений каналов.
// Высота столбца = текущий код канала; столбцы «колеблются» по мере поступления данных.
// (Графики «значение от времени» добавим позже отдельной вкладкой.)
class PlotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PlotWidget(QWidget* parent = nullptr);

    void setValues(const std::vector<double>& values,
                   const std::vector<std::string>& names);
    void setChannelNames(const std::vector<std::string>& names);
    void clearAll();

    // Совместимость с панелью настроек (актуально для будущего time-series графика).
    void setWindowSeconds(double) {}

private:
    QCustomPlot*       plot_;
    QCPBars*           bars_;
    QCPAxisTickerText* ticker_;
    std::vector<std::string> lastNames_;
};

#endif // PLOT_WIDGET_H
