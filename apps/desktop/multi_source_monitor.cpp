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
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QCryptographicHash>
#include <algorithm>

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
    auto* pageControls = new QHBoxLayout;
    pageControls->addWidget(new QLabel(QStringLiteral("Страница"), this));
    pages_ = new QComboBox(this);
    pages_->setObjectName(QStringLiteral("monitorPages"));
    pageControls->addWidget(pages_, 1);
    auto* createPage = new QPushButton(QStringLiteral("Создать страницу"), this);
    createPage->setObjectName(QStringLiteral("createMonitorPage"));
    auto* editPageButton = new QPushButton(QStringLiteral("Изменить"), this);
    editPageButton->setObjectName(QStringLiteral("editMonitorPage"));
    pageControls->addWidget(createPage);
    pageControls->addWidget(editPageButton);
    layout->addLayout(pageControls);
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
    splitter_ = splitter;
    splitter->addWidget(table_);
    auto* graphPanel = new QWidget(splitter);
    graphPanel_ = graphPanel;
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
    connect(pages_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { applyPage(true); });
    connect(createPage, &QPushButton::clicked, this, [this] { editPage(true); });
    connect(editPageButton, &QPushButton::clicked, this, [this] { editPage(false); });
    refreshPages();
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
    availableBindings_ = std::move(bindings);
    applyPage(false);
}

void MultiSourceMonitor::rebuildBindings(std::vector<MonitoredBinding> bindings)
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

namespace {
QString pageDirectory(const QString& scope)
{
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty() || scope.isEmpty()) return {};
    const auto key = QCryptographicHash::hash(scope.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(root).filePath(QStringLiteral("monitor-pages/") + QString::fromLatin1(key));
}

QJsonObject readPage(const QString& scope, const QString& id, QString* error)
{
    if (pageDirectory(scope).isEmpty()) { *error = QStringLiteral("Не задана поставка или каталог данных"); return {}; }
    if (QUuid(id).isNull()) { *error = QStringLiteral("Некорректный идентификатор страницы"); return {}; }
    QFile file(QDir(pageDirectory(scope)).filePath(id + QStringLiteral(".json")));
    if (!file.open(QIODevice::ReadOnly)) { *error = file.errorString(); return {}; }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("Не удалось прочитать страницу: %1").arg(parseError.errorString());
        return {};
    }
    const auto result = document.object();
    if (result.value("version").toInt() != 1 || !result.value("title").isString()
        || !result.value("parameters").isArray()) {
        *error = QStringLiteral("Неподдерживаемый формат страницы"); return {};
    }
    for (const auto& value : result.value("parameters").toArray()) {
        const auto entry = value.toObject();
        if (!value.isObject() || entry.value("source").toString().isEmpty()
            || entry.value("key").toString().isEmpty() || !entry.value("label").isString()) {
            *error = QStringLiteral("Некорректный параметр страницы"); return {};
        }
    }
    return result;
}

class PageDialog final : public QDialog {
public:
    using QDialog::QDialog;
    bool changed = false;
    void reject() override
    {
        if (changed && QMessageBox::question(this, QStringLiteral("Несохранённые изменения"),
            QStringLiteral("Закрыть редактор и потерять изменения?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        QDialog::reject();
    }
};
}

void MultiSourceMonitor::setPageScope(const QString& scope)
{
    if (pageScope_ == scope) return;
    pageScope_ = scope;
    refreshPages();
    applyPage(true);
}

void MultiSourceMonitor::refreshPages(const QString& selectedId)
{
    const QSignalBlocker blocker(pages_);
    pages_->clear();
    pages_->addItem(QStringLiteral("Все параметры"), QString());
    if (pageDirectory(pageScope_).isEmpty()) return;
    const QDir dir(pageDirectory(pageScope_));
    for (const auto& filename : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        const QString id = filename.left(filename.size() - 5);
        QString error;
        const auto page = readPage(pageScope_, id, &error);
        pages_->addItem(error.isEmpty() ? page.value("title").toString()
                                      : QStringLiteral("Ошибка страницы: %1").arg(filename), id);
    }
    const int index = pages_->findData(selectedId);
    pages_->setCurrentIndex(index < 0 ? 0 : index);
}

void MultiSourceMonitor::applyPage(bool resetSelection)
{
    const auto id = pages_->currentData().toString();
    if (id.isEmpty()) {
        table_->show(); graphPanel_->show();
        splitter_->insertWidget(0, table_);
        rebuildBindings(availableBindings_);
        if (resetSelection) {
            for (int row = 0; row < table_->rowCount(); ++row)
                table_->item(row, 0)->setCheckState(Qt::Unchecked);
        }
        return;
    }
    QString error;
    const auto page = readPage(pageScope_, id, &error);
    if (!error.isEmpty()) {
        rebuildBindings({});
        QMessageBox::warning(this, QStringLiteral("Страница мониторинга"), error);
        return;
    }
    table_->setVisible(page.value("table").toBool(true));
    graphPanel_->setVisible(page.value("graph").toBool(true));
    splitter_->insertWidget(0, page.value("graphFirst").toBool() ? graphPanel_ : table_);
    std::vector<MonitoredBinding> visible;
    const auto parameters = page.value("parameters").toArray();
    for (const auto& value : parameters) {
        const auto item = value.toObject();
        const auto source = item.value("source").toString();
        const auto key = item.value("key").toString();
        auto it = std::find_if(availableBindings_.cbegin(), availableBindings_.cend(),
            [&](const MonitoredBinding& binding) { return binding.sourceId == source && binding.parameterKey == key; });
        MonitoredBinding binding = it == availableBindings_.cend()
            ? MonitoredBinding{source, item.value("sourceTitle").toString(source), key,
                item.value("label").toString(), QStringLiteral("Параметр отсутствует в текущем профиле"),
                item.value("unit").toString()}
            : *it;
        binding.parameterTitle = item.value("label").toString();
        if (it != availableBindings_.cend() && item.value("unit").toString() != binding.unit)
            binding.unavailableReason = QStringLiteral("Единица параметра изменилась; проверьте страницу");
        visible.push_back(std::move(binding));
    }
    rebuildBindings(std::move(visible));
    if (resetSelection) {
        for (int row = 0; row < parameters.size(); ++row)
            table_->item(row, 0)->setCheckState(parameters[row].toObject().value("plot").toBool()
                                                ? Qt::Checked : Qt::Unchecked);
    }
}

void MultiSourceMonitor::editPage(bool create)
{
    if (pageDirectory(pageScope_).isEmpty()) { QMessageBox::warning(this, QStringLiteral("Страница"),
        QStringLiteral("Для страниц нужны загруженная поставка, профиль оборудования и каталог данных.")); return; }
    QString id = create ? QUuid::createUuid().toString(QUuid::WithoutBraces) : pages_->currentData().toString();
    if (id.isEmpty()) { QMessageBox::information(this, QStringLiteral("Страница"),
        QStringLiteral("Создайте страницу, чтобы настроить её.")); return; }
    QString error;
    const auto old = create ? QJsonObject{} : readPage(pageScope_, id, &error);
    if (!error.isEmpty()) { QMessageBox::warning(this, QStringLiteral("Страница"), error); return; }
    PageDialog dialog(this);
    dialog.setWindowTitle(create ? QStringLiteral("Новая страница мониторинга") : QStringLiteral("Редактор страницы"));
    dialog.resize(800, 500);
    auto* layout = new QVBoxLayout(&dialog);
    auto* title = new QLineEdit(old.value("title").toString(), &dialog);
    title->setObjectName(QStringLiteral("pageTitle"));
    title->setPlaceholderText(QStringLiteral("Название страницы"));
    layout->addWidget(title);
    auto* list = new QTableWidget(&dialog);
    list->setObjectName(QStringLiteral("pageParameters"));
    list->setColumnCount(4);
    list->setHorizontalHeaderLabels({QStringLiteral("Показывать"), QStringLiteral("Источник"),
                                     QStringLiteral("Подпись параметра"), QStringLiteral("График")});
    list->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    list->verticalHeader()->hide();
    list->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(list, 1);
    auto add = [&](const MonitoredBinding& binding, bool selected, bool plotted) {
        const int row = list->rowCount(); list->insertRow(row);
        auto* include = new QTableWidgetItem;
        include->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        include->setCheckState(selected ? Qt::Checked : Qt::Unchecked);
        include->setData(Qt::UserRole, binding.sourceId);
        include->setData(Qt::UserRole + 1, binding.parameterKey);
        include->setData(Qt::UserRole + 2, binding.unit);
        include->setToolTip(QStringLiteral("Сохранённая единица: %1").arg(binding.unit));
        list->setItem(row, 0, include);
        auto* source = new QTableWidgetItem(binding.sourceTitle);
        source->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        source->setToolTip(binding.unavailableReason);
        list->setItem(row, 1, source);
        list->setItem(row, 2, new QTableWidgetItem(binding.parameterTitle));
        auto* graph = new QTableWidgetItem;
        graph->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        graph->setCheckState(plotted ? Qt::Checked : Qt::Unchecked);
        list->setItem(row, 3, graph);
    };
    const auto entries = old.value("parameters").toArray();
    for (const auto& value : entries) {
        const auto item = value.toObject();
        const auto source = item.value("source").toString(), key = item.value("key").toString();
        auto it = std::find_if(availableBindings_.cbegin(), availableBindings_.cend(),
            [&](const MonitoredBinding& binding) { return binding.sourceId == source && binding.parameterKey == key; });
        MonitoredBinding b = it == availableBindings_.cend()
            ? MonitoredBinding{source, item.value("sourceTitle").toString(source), key, {},
                QStringLiteral("Параметр отсутствует в текущем профиле"), item.value("unit").toString()}
            : *it;
        b.parameterTitle = item.value("label").toString();
        // Сохранённая единица не меняется автоматически при смене профиля.
        b.unit = item.value("unit").toString();
        add(b, true, item.value("plot").toBool());
    }
    for (const auto& b : availableBindings_) {
        bool exists = false;
        for (const auto& value : entries) {
            const auto item = value.toObject();
            if (item.value("source").toString() == b.sourceId && item.value("key").toString() == b.parameterKey)
                { exists = true; break; }
        }
        if (!exists) add(b, false, false);
    }
    auto* order = new QHBoxLayout;
    auto* up = new QPushButton(QStringLiteral("Выше"), &dialog);
    auto* down = new QPushButton(QStringLiteral("Ниже"), &dialog);
    auto* acceptUnit = new QPushButton(QStringLiteral("Принять текущую единицу"), &dialog);
    order->addWidget(up); order->addWidget(down); order->addWidget(acceptUnit); order->addStretch();
    layout->addLayout(order);
    auto move = [list, &dialog](int delta) {
        const int from = list->currentRow(), to = from + delta;
        if (from < 0 || to < 0 || to >= list->rowCount()) return;
        for (int col = 0; col < list->columnCount(); ++col) {
            auto* first = list->takeItem(from, col); auto* second = list->takeItem(to, col);
            list->setItem(from, col, second); list->setItem(to, col, first);
        }
        list->selectRow(to); dialog.changed = true;
    };
    QObject::connect(up, &QPushButton::clicked, &dialog, [move] { move(-1); });
    QObject::connect(down, &QPushButton::clicked, &dialog, [move] { move(1); });
    QObject::connect(acceptUnit, &QPushButton::clicked, &dialog, [this, list, &dialog] {
        const int row = list->currentRow();
        if (row < 0) return;
        auto* item = list->item(row, 0);
        const auto it = std::find_if(availableBindings_.cbegin(), availableBindings_.cend(),
            [item](const MonitoredBinding& binding) {
                return binding.sourceId == item->data(Qt::UserRole).toString()
                    && binding.parameterKey == item->data(Qt::UserRole + 1).toString();
            });
        if (it == availableBindings_.cend()) {
            QMessageBox::information(&dialog, QStringLiteral("Единица"), QStringLiteral("Параметр недоступен в профиле."));
            return;
        }
        const auto previous = item->data(Qt::UserRole + 2).toString();
        if (previous == it->unit) return;
        if (QMessageBox::question(&dialog, QStringLiteral("Изменение единицы"),
            QStringLiteral("Заменить единицу «%1» на «%2»? Проверьте настройки графика.")
                .arg(previous, it->unit), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        item->setData(Qt::UserRole + 2, it->unit);
        item->setToolTip(QStringLiteral("Сохранённая единица: %1").arg(it->unit));
        dialog.changed = true;
    });
    auto* blocks = new QHBoxLayout;
    auto* showTable = new QCheckBox(QStringLiteral("Таблица"), &dialog);
    auto* showGraph = new QCheckBox(QStringLiteral("График"), &dialog);
    showTable->setChecked(old.value("table").toBool(true));
    showGraph->setChecked(old.value("graph").toBool(true));
    auto* blockOrder = new QComboBox(&dialog);
    blockOrder->addItems({QStringLiteral("Таблица сверху"), QStringLiteral("График сверху")});
    blockOrder->setCurrentIndex(old.value("graphFirst").toBool() ? 1 : 0);
    blocks->addWidget(showTable); blocks->addWidget(showGraph); blocks->addWidget(blockOrder);
    layout->addLayout(blocks);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(title, &QLineEdit::textChanged, &dialog, [&dialog] { dialog.changed = true; });
    QObject::connect(list, &QTableWidget::itemChanged, &dialog, [&dialog] { dialog.changed = true; });
    QObject::connect(showTable, &QCheckBox::toggled, &dialog, [&dialog] { dialog.changed = true; });
    QObject::connect(showGraph, &QCheckBox::toggled, &dialog, [&dialog] { dialog.changed = true; });
    QObject::connect(blockOrder, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&dialog] { dialog.changed = true; });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QJsonArray parameters;
        for (int row = 0; row < list->rowCount(); ++row) {
            auto* include = list->item(row, 0);
            if (include->checkState() != Qt::Checked) continue;
            const auto label = list->item(row, 2)->text().trimmed();
            if (label.isEmpty()) { QMessageBox::warning(&dialog, QStringLiteral("Страница"),
                QStringLiteral("Укажите подпись каждого выбранного параметра.")); return; }
            parameters.append(QJsonObject{{"source", include->data(Qt::UserRole).toString()},
                {"key", include->data(Qt::UserRole + 1).toString()},
                {"unit", include->data(Qt::UserRole + 2).toString()},
                {"sourceTitle", list->item(row, 1)->text()}, {"label", label},
                {"plot", list->item(row, 3)->checkState() == Qt::Checked}});
        }
        if (title->text().trimmed().isEmpty() || parameters.isEmpty() || (!showTable->isChecked() && !showGraph->isChecked())) {
            QMessageBox::warning(&dialog, QStringLiteral("Страница"),
                QStringLiteral("Укажите название, выберите параметры и хотя бы один компонент.")); return;
        }
        if (!QDir().mkpath(pageDirectory(pageScope_))) { QMessageBox::warning(&dialog, QStringLiteral("Страница"),
            QStringLiteral("Не удалось создать каталог страниц.")); return; }
        QSaveFile file(QDir(pageDirectory(pageScope_)).filePath(id + QStringLiteral(".json")));
        const QJsonObject page{{"version", 1}, {"title", title->text().trimmed()}, {"parameters", parameters},
            {"table", showTable->isChecked()}, {"graph", showGraph->isChecked()},
            {"graphFirst", blockOrder->currentIndex() == 1}};
        const auto data = QJsonDocument(page).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            QMessageBox::warning(&dialog, QStringLiteral("Страница"),
                                 QStringLiteral("Не удалось сохранить страницу: %1").arg(file.errorString())); return;
        }
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) { refreshPages(id); applyPage(true); }
}
