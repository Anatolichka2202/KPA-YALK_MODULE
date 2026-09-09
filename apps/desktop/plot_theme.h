#pragma once
/**
 * @file plot_theme.h
 * @brief Тёмная тема для QCustomPlot. QSS на него не действует (виджет рисует
 *        себя сам), поэтому фон/оси/сетку/подписи задаём программно.
 */

#include <qcustomplot.h>

namespace plottheme {

inline void apply(QCustomPlot* plot) {
    if (!plot) return;

    const QColor bg(0x0e, 0x11, 0x15);   // фон как у списков/таблиц
    const QColor fg(0x7e, 0x8a, 0x98);   // оси, подписи
    const QColor grid(0x1f, 0x26, 0x2f); // сетка

    plot->setBackground(QBrush(bg));
    if (plot->axisRect())
        plot->axisRect()->setBackground(QBrush(bg));

    QCPAxis* axes[] = { plot->xAxis, plot->yAxis, plot->xAxis2, plot->yAxis2 };
    for (QCPAxis* ax : axes) {
        if (!ax) continue;
        ax->setBasePen(QPen(fg));
        ax->setTickPen(QPen(fg));
        ax->setSubTickPen(QPen(grid));
        ax->setTickLabelColor(fg);
        ax->setLabelColor(fg);
        ax->grid()->setPen(QPen(grid, 0, Qt::DotLine));
        ax->grid()->setSubGridVisible(false);
    }
}

} // namespace plottheme
