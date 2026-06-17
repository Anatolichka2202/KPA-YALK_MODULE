#pragma once

#include <QWidget>
#include <qcustomplot.h>
#include <vector>
#include <QMap>
#include "orbita.h"
#include "metadata_service.h"

class ToleranceResolver;

class BarChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BarChartWidget(QWidget *parent = nullptr);
    void setMetadataService(MetadataService* db);
    void setToleranceResolver(ToleranceResolver* r);
    void setChannels(const std::vector<orbita::ChannelSpec>& specs);
    void updateValues(const QMap<QString, double>& values);
    void setSelectedBar(int index);
    void setHoveredBar(int index);

signals:
    void barClicked(int index);
    void barDoubleClicked(int index);
    void barHovered(int index);

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void setupPlot();
    void updatePlot();

    QCustomPlot* m_plot;
    QCPBars* m_barsNormal  = nullptr;  // синий — норма
    QCPBars* m_barsWarning = nullptr;  // янтарный — у предела
    QCPBars* m_barsError   = nullptr;  // красный — вне допуска
    QCPBars* m_selBars     = nullptr;  // выделенный (белая рамка)
    QCPItemText* m_valueText = nullptr;
    QVector<double> m_keys;
    QVector<double> m_valuesPlot;
    std::vector<orbita::ChannelSpec> m_specs;
    QMap<QString, double> m_values;
    MetadataService* m_db = nullptr;
    ToleranceResolver* m_resolver = nullptr;
    int m_selectedIndex = -1;
    int m_hoveredIndex = -1;
    bool m_mouseTracking = false;
    QVector<QCPItemStraightLine*> m_separators;
};
