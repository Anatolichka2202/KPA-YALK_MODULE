#include "bar_chart_widget.h"
#include "tolerance_resolver.h"
#include "plot_theme.h"
#include <QVBoxLayout>
#include <QMouseEvent>

BarChartWidget::BarChartWidget(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_plot = new QCustomPlot;
    layout->addWidget(m_plot);
    setupPlot();
    setMouseTracking(true);
}

void BarChartWidget::setupPlot()
{
    plottheme::apply(m_plot);
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    // Масштабировать/таскать только по X — Y зафиксирован, нельзя улететь в минус
    m_plot->axisRect()->setRangeDrag(Qt::Horizontal);
    m_plot->axisRect()->setRangeZoom(Qt::Horizontal);
    m_plot->yAxis->setLabel("Код");
    m_plot->yAxis->setRange(0, 1023);
    m_plot->xAxis->setTickLabelRotation(60);
    m_plot->xAxis->setSubTicks(false);
    m_plot->xAxis->setTicks(true);   // подписи имён каналов под столбцами

    m_bars = new QCPBars(m_plot->xAxis, m_plot->yAxis);
    m_bars->setPen(QPen(QColor(0x5e, 0x93, 0xb8)));
    m_bars->setBrush(QBrush(QColor(0x5e, 0x93, 0xb8)));
    m_bars->setWidthType(QCPBars::wtPlotCoords);
    m_bars->setWidth(0.7);

    // Текстовый тикер для меток
    QSharedPointer<QCPAxisTickerText> textTicker(new QCPAxisTickerText);
    m_plot->xAxis->setTicker(textTicker);
}

void BarChartWidget::setMetadataService(MetadataService* db)
{
    m_db = db;
}

void BarChartWidget::setToleranceResolver(ToleranceResolver* r)
{
    m_resolver = r;
}

void BarChartWidget::setChannels(const std::vector<orbita::ChannelSpec>& specs)
{
    m_specs = specs;
    updatePlot();
}

void BarChartWidget::updateValues(const QMap<QString, double>& values)
{
    m_values = values;
    updatePlot();
}

void BarChartWidget::updatePlot()
{
    int n = m_specs.size();
    if (n == 0) {
        m_bars->data()->clear();
        m_plot->replot();
        return;
    }

    m_keys.resize(n);
    m_valuesPlot.resize(n);
    QMap<double, QString> tickLabels;
    for (int i = 0; i < n; ++i) {
        m_keys[i] = i;
        double val = m_values.value(QString::fromStdString(m_specs[i].address), 0.0);
        m_valuesPlot[i] = val;
        QString name = QString::fromStdString(m_specs[i].name.empty() ? m_specs[i].address : m_specs[i].name);
        // Сокращаем имена до 3 символов для читаемости
        if (name.length() > 8) name = name.left(6) + "…";
        tickLabels[i] = name;
    }

    m_bars->setData(m_keys, m_valuesPlot);
    QCPAxisTickerText* ticker = dynamic_cast<QCPAxisTickerText*>(m_plot->xAxis->ticker().data());
    if (ticker) {
        ticker->clear();
        ticker->addTicks(tickLabels);
    }
    m_plot->xAxis->setRange(-0.5, n - 0.5);

    // Y: всегда от 0, потолок по данным (но не ниже 1023) — без отрицательных
    double maxVal = 0.0;
    for (double v : m_valuesPlot) maxVal = qMax(maxVal, v);
    m_plot->yAxis->setRange(0, qMax(1023.0, maxVal * 1.05));

    m_plot->replot();
}

void BarChartWidget::setSelectedBar(int index)
{
    m_selectedIndex = index;
    // Обновляем внешний вид столбцов (можно изменить цвет)
    // Пока просто перерисовываем
    updatePlot();
}

void BarChartWidget::setHoveredBar(int index)
{
    m_hoveredIndex = index;
    // Можно подсветить столбец
    updatePlot();
}

void BarChartWidget::mouseMoveEvent(QMouseEvent* event)
{
    // Определяем, над каким столбцом курсор
    double key = m_plot->xAxis->pixelToCoord(event->pos().x());
    int index = qRound(key);
    if (index >= 0 && index < m_specs.size() && index != m_hoveredIndex) {
        setHoveredBar(index);
        emit barHovered(index); // если нужен сигнал
    } else if (index < 0 || index >= m_specs.size()) {
        setHoveredBar(-1);
    }
    QWidget::mouseMoveEvent(event);
}

void BarChartWidget::mousePressEvent(QMouseEvent* event)
{
    double key = m_plot->xAxis->pixelToCoord(event->pos().x());
    int index = qRound(key);
    if (index >= 0 && index < m_specs.size()) {
        emit barClicked(index);
    }
    QWidget::mousePressEvent(event);
}

void BarChartWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    double key = m_plot->xAxis->pixelToCoord(event->pos().x());
    int index = qRound(key);
    if (index >= 0 && index < m_specs.size()) {
        emit barDoubleClicked(index);
    }
    QWidget::mouseDoubleClickEvent(event);
}
