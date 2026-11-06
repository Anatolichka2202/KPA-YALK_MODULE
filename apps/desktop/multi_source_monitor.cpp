#include "multi_source_monitor.h"
#include "plot_theme.h"

#include <QHeaderView>
#include <QCheckBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSplitter>
#include <limits>
#include <cmath>
#include <QLabel>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <chrono>

MultiSourceMonitor::MultiSourceMonitor(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* heading = new QLabel(QStringLiteral("ПАРАМЕТРЫ ИСТОЧНИКОВ"), this);
    heading->setStyleSheet(QStringLiteral("font-weight:700;color:#9ac7ff;"));
    layout->addWidget(heading);
    auto* explanation = new QLabel(QStringLiteral(
        "Значения отображаются по источникам. Для Орбиты выберите каналы и запустите сбор."), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("multiSourceValues"));
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({QStringLiteral("Источник"), QStringLiteral("Параметр"),
        QStringLiteral("Значение"), QStringLiteral("Ед."), QStringLiteral("Состояние")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(table_);
    auto* graphPanel = new QWidget(splitter);
    auto* graphLayout = new QVBoxLayout(graphPanel);
    graphLayout->setContentsMargins(0, 0, 0, 0);
    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel(QStringLiteral("Отметьте параметры в таблице для графика"), graphPanel));
    followTime_ = new QCheckBox(QStringLiteral("Последняя минута"), graphPanel);
    followTime_->setObjectName(QStringLiteral("trendFollowTime"));
    followTime_->setChecked(true);
    automaticScale_ = new QCheckBox(QStringLiteral("Автошкалы"), graphPanel);
    automaticScale_->setObjectName(QStringLiteral("trendAutoScale"));
    automaticScale_->setChecked(true);
    controls->addStretch();
    controls->addWidget(followTime_);
    controls->addWidget(automaticScale_);
    graphLayout->addLayout(controls);
    plot_ = new QCustomPlot(graphPanel);
    plot_->setObjectName(QStringLiteral("sourceTrendPlot"));
    plot_->setMinimumHeight(220);
    plottheme::apply(plot_);
    plot_->legend->setBrush(QColor(0x0e, 0x11, 0x15));
    plot_->legend->setTextColor(QColor(0xd7, 0xe8, 0xf5));
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    plot_->xAxis->setLabel(QStringLiteral("Время"));
    plot_->yAxis->setVisible(false);
    auto timeTicker = QSharedPointer<QCPAxisTickerDateTime>::create();
    timeTicker->setDateTimeFormat(QStringLiteral("HH:mm:ss"));
    plot_->xAxis->setTicker(timeTicker);
    plot_->legend->setVisible(true);
    graphLayout->addWidget(plot_, 1);
    auto* hint = new QLabel(QStringLiteral(
        "График показывает наблюдаемые снимки за последние 10 минут; это не запись полного потока."), graphPanel);
    hint->setWordWrap(true);
    graphLayout->addWidget(hint);
    splitter->addWidget(graphPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item->column() == 0) updateVisibility();
    });
    timer_ = new QTimer(this);
    timer_->setInterval(250);
    connect(timer_, &QTimer::timeout, this, &MultiSourceMonitor::display);
}

MultiSourceMonitor::~MultiSourceMonitor() = default;

void MultiSourceMonitor::setRepository(orbita::stand::ParameterRepository* repository)
{
    if (repository_ != repository) {
        // Одинаковое имя источника в другом профиле не доказывает, что это
        // тот же прибор. История и диапазоны предыдущей сессии не переносятся.
        for (auto& item : series_) {
            item.graph->data()->clear();
            item.axis->setRange(0, 1);
            item.lastSequence = 0;
            item.lastTimestamp = {};
            item.gapTime = 0;
            item.hasSample = false;
            item.gap = false;
        }
    }
    repository_ = repository;
    if (repository_) timer_->start(); else timer_->stop();
    display();
}

void MultiSourceMonitor::setBindings(std::vector<MonitoredBinding> bindings)
{
    const QSignalBlocker block(table_);
    struct SavedSeries {
        Series state;
        QSharedPointer<QCPGraphDataContainer> data;
        QCPRange range;
        QString unit;
        bool selected = false;
    };
    QMap<QPair<QString, QString>, SavedSeries> saved;
    for (int row = 0; row < static_cast<int>(bindings_.size()); ++row) {
        const auto& binding = bindings_[row];
        const auto& series = series_[row];
        saved.insert({binding.sourceId, binding.parameterKey},
            {series, series.graph->data(), series.axis->range(), binding.unit,
             table_->item(row, 0)->checkState() == Qt::Checked});
    }
    plot_->clearGraphs();
    for (const auto& item : series_) plot_->axisRect()->removeAxis(item.axis);
    series_.clear();
    bindings_ = std::move(bindings);
    table_->setRowCount(static_cast<int>(bindings_.size()));
    for (int row = 0; row < table_->rowCount(); ++row) {
        for (int col = 0; col < table_->columnCount(); ++col)
            if (!table_->item(row, col)) table_->setItem(row, col, new QTableWidgetItem);
        table_->item(row, 0)->setText(bindings_[row].sourceTitle);
        table_->item(row, 0)->setFlags(table_->item(row, 0)->flags() | Qt::ItemIsUserCheckable);
        table_->item(row, 0)->setCheckState(Qt::Unchecked);
        table_->item(row, 1)->setText(bindings_[row].parameterTitle);
        table_->item(row, 1)->setToolTip(bindings_[row].parameterKey);
        auto* axis = plot_->axisRect()->addAxis(row % 2 ? QCPAxis::atRight : QCPAxis::atLeft);
        auto* graph = plot_->addGraph(plot_->xAxis, axis);
        const auto color = QColor::fromHsv((row * 79 + 205) % 360, 190, 220);
        graph->setPen(QPen(color, 2));
        graph->setName(bindings_[row].sourceTitle + QStringLiteral(" · ") + bindings_[row].parameterTitle);
        axis->setLabel(bindings_[row].parameterTitle + (bindings_[row].unit.isEmpty()
            ? QString() : QStringLiteral(" · ") + bindings_[row].unit));
        axis->setLabelColor(color);
        axis->setTickLabelColor(color);
        axis->setBasePen(QPen(color));
        axis->setTickPen(QPen(color));
        axis->setSubTickPen(QPen(color));
        const auto savedItem = saved.constFind({bindings_[row].sourceId, bindings_[row].parameterKey});
        if (savedItem != saved.cend() && savedItem->unit == bindings_[row].unit) {
            auto restored = savedItem->state;
            restored.graph = graph;
            restored.axis = axis;
            graph->setData(savedItem->data);
            axis->setRange(savedItem->range);
            table_->item(row, 0)->setCheckState(savedItem->selected ? Qt::Checked : Qt::Unchecked);
            series_.push_back(restored);
        } else {
            series_.push_back({graph, axis});
        }
        graph->setVisible(false);
        axis->setVisible(false);
    }
    updateVisibility();
    display();
}

void MultiSourceMonitor::display()
{
    using orbita::stand::SampleQuality;
    for (int row = 0; row < static_cast<int>(bindings_.size()); ++row) {
        QString value = QStringLiteral("—"), unit, status = QStringLiteral("Нет данных"), diagnostic;
        std::optional<orbita::stand::ParameterSample> graphSample;
        if (!bindings_[row].unavailableReason.isEmpty()) {
            status = QStringLiteral("Недоступен");
            diagnostic = bindings_[row].unavailableReason;
        } else if (repository_ && !bindings_[row].parameterKey.isEmpty()) {
            try {
                const auto sample = repository_->latest(bindings_[row].sourceId.toStdString(),
                    bindings_[row].parameterKey.toStdString(), std::chrono::seconds(5));
                if (sample) {
                    graphSample = sample;
                    diagnostic = QString::fromStdString(sample->diagnostic);
                    switch (sample->quality) {
                    case SampleQuality::Good:
                        unit = QString::fromStdString(sample->unit);
                        if (!std::isfinite(sample->physicalValue)) {
                            graphSample.reset();
                            status = QStringLiteral("Недостоверно");
                            diagnostic = QStringLiteral("Источник вернул нечисловое или бесконечное значение");
                        } else if (!bindings_[row].unit.isEmpty() && bindings_[row].unit != unit) {
                            status = QStringLiteral("Единица изменилась");
                            diagnostic = QStringLiteral("График приостановлен: ожидается %1, источник сообщает %2")
                                .arg(bindings_[row].unit, unit);
                            graphSample.reset();
                        } else {
                            value = QString::number(sample->physicalValue, 'g', 12);
                            status = QStringLiteral("Актуально");
                            if (bindings_[row].unit.isEmpty()) {
                                graphSample.reset();
                                status = QStringLiteral("Актуально · нет единицы графика");
                                diagnostic = QStringLiteral("График недоступен: единица источника не закреплена в настройках");
                            }
                        }
                        break;
                    case SampleQuality::Stale: status = QStringLiteral("Устарело"); break;
                    case SampleQuality::Disconnected: status = QStringLiteral("Отключён"); break;
                    case SampleQuality::Invalid: status = QStringLiteral("Нет данных"); break;
                    }
                }
            } catch (const std::exception& error) {
                status = QStringLiteral("Недоступен");
                diagnostic = QString::fromUtf8(error.what());
            }
        }
        table_->item(row, 2)->setText(value);
        table_->item(row, 3)->setText(unit);
        table_->item(row, 4)->setText(status);
        table_->item(row, 4)->setToolTip(diagnostic);
        updateGraph(row, graphSample);
    }
    const auto now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    if (followTime_->isChecked()) plot_->xAxis->setRange(now - 60.0, now);
    if (automaticScale_->isChecked())
        for (const auto& item : series_) if (item.graph->visible() && item.hasSample)
            item.graph->rescaleValueAxis(false, true);
    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void MultiSourceMonitor::updateGraph(int row, const std::optional<orbita::stand::ParameterSample>& sample)
{
    auto& series = series_[static_cast<std::size_t>(row)];
    if (!series.graph->visible()) return;
    const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    if (!sample || sample->quality != orbita::stand::SampleQuality::Good
        || !std::isfinite(sample->physicalValue)) {
        if (series.hasSample && !series.gap) {
            series.graph->addData(now, std::numeric_limits<double>::quiet_NaN());
            series.gap = true;
            series.gapTime = now;
        }
    } else if (!series.hasSample || sample->sequence != series.lastSequence
               || sample->timestamp != series.lastTimestamp) {
        const double time = std::chrono::duration<double>(sample->timestamp.time_since_epoch()).count();
        // После сбоя не соединяем пропуск со старым снимком, пришедшим поздно.
        if (!series.gap || time > series.gapTime) {
            series.graph->addData(time, sample->physicalValue);
            series.lastSequence = sample->sequence;
            series.lastTimestamp = sample->timestamp;
            series.hasSample = true;
            series.gap = false;
        }
    }
    series.graph->data()->removeBefore(now - 600.0);
}

void MultiSourceMonitor::updateVisibility()
{
    QList<QCPAxis*> vertical;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const bool selected = table_->item(row, 0)->checkState() == Qt::Checked;
        auto& series = series_[static_cast<std::size_t>(row)];
        series.graph->setVisible(selected);
        series.axis->setVisible(selected);
        if (selected) vertical.push_back(series.axis);
        else if (series.hasSample && !series.gap) {
            // При повторном включении не соединяем показания через скрытый период.
            series.graph->addData(QDateTime::currentMSecsSinceEpoch() / 1000.0,
                                  std::numeric_limits<double>::quiet_NaN());
            series.gap = true;
            series.gapTime = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        }
    }
    plot_->axisRect()->setRangeDragAxes({plot_->xAxis}, vertical);
    plot_->axisRect()->setRangeZoomAxes({plot_->xAxis}, vertical);
    plot_->replot(QCustomPlot::rpQueuedReplot);
}
