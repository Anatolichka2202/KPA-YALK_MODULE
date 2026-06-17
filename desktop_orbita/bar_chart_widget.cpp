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
    m_plot->xAxis->setTickLabelRotation(0);
    m_plot->xAxis->setSubTicks(false);
    m_plot->xAxis->setTicks(true);   // подписи категорий под группами столбцов

    m_bars = new QCPBars(m_plot->xAxis, m_plot->yAxis);
    m_bars->setPen(QPen(QColor(0x5e, 0x93, 0xb8)));
    m_bars->setBrush(QBrush(QColor(0x5e, 0x93, 0xb8)));
    m_bars->setWidthType(QCPBars::wtPlotCoords);
    m_bars->setWidth(0.7);

    // Выделенный столбец (амбер)
    m_selBars = new QCPBars(m_plot->xAxis, m_plot->yAxis);
    m_selBars->setPen(QPen(QColor(0xd9, 0x9a, 0x4a)));
    m_selBars->setBrush(QBrush(QColor(0xd9, 0x9a, 0x4a)));
    m_selBars->setWidthType(QCPBars::wtPlotCoords);
    m_selBars->setWidth(0.7);

    // Метка значения над выделенным столбцом
    m_valueText = new QCPItemText(m_plot);
    m_valueText->setColor(QColor(0xe6, 0xea, 0xf0));
    m_valueText->setFont(QFont("IBM Plex Mono", 11, QFont::Bold));
    m_valueText->setPositionAlignment(Qt::AlignBottom | Qt::AlignHCenter);
    m_valueText->setLayer("overlay");
    m_valueText->setVisible(false);

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

    // Удалить старые разделители перед перестройкой
    for (auto* s : m_separators) m_plot->removeItem(s);
    m_separators.clear();

    m_keys.resize(n);
    m_valuesPlot.resize(n);
    for (int i = 0; i < n; ++i) {
        m_keys[i] = i;
        double val = m_values.value(QString::fromStdString(m_specs[i].address), 0.0);
        m_valuesPlot[i] = val;
    }

    // Строим непрерывные серии каналов с одинаковой категорией
    // и добавляем один тик в центре каждой серии
    QMap<double, QString> tickLabels;
    {
        int i = 0;
        while (i < n) {
            // Получаем категорию текущего канала
            QString cat;
            if (m_db) {
                auto info = m_db->lookup(QString::fromStdString(m_specs[i].address));
                if (info) cat = info->category;
            }
            if (cat.isEmpty()) cat = QStringLiteral("—");

            // Расширяем серию пока категория совпадает
            int start = i;
            while (i < n) {
                QString nextCat;
                if (m_db) {
                    auto info = m_db->lookup(QString::fromStdString(m_specs[i].address));
                    if (info) nextCat = info->category;
                }
                if (nextCat.isEmpty()) nextCat = QStringLiteral("—");
                if (nextCat != cat) break;
                ++i;
            }
            int end = i - 1; // включительно

            // Тик в центре серии
            double centerKey = (start + end) / 2.0;
            tickLabels[centerKey] = cat;

            // Разделитель перед следующей серией (между end и i)
            if (i < n) {
                double boundaryX = end + 0.5;
                auto* sep = new QCPItemStraightLine(m_plot);
                sep->setPen(QPen(QColor(0x23, 0x2a, 0x33), 0, Qt::DotLine));
                sep->setSelectable(false);
                sep->point1->setCoords(boundaryX, 0);
                sep->point2->setCoords(boundaryX, 1023);
                m_separators.append(sep);
            }
        }
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

    // Выделенный столбец + подпись значения
    if (m_selBars && m_valueText) {
        if (m_selectedIndex >= 0 && m_selectedIndex < m_valuesPlot.size()) {
            double selKey = static_cast<double>(m_selectedIndex);
            double selVal = m_valuesPlot[m_selectedIndex];
            QVector<double> sk{selKey}, sv{selVal};
            m_selBars->setData(sk, sv);
            m_valueText->setVisible(true);
            m_valueText->position->setCoords(selKey, selVal);
            m_valueText->setText(QString::number(selVal, 'f', 0));
        } else {
            m_selBars->data()->clear();
            m_valueText->setVisible(false);
        }
    }

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
