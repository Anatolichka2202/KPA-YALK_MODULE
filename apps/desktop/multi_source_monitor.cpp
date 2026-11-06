#include "multi_source_monitor.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <chrono>
#include <map>

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
    layout->addWidget(table_);
    timer_ = new QTimer(this);
    timer_->setInterval(250);
    connect(timer_, &QTimer::timeout, this, &MultiSourceMonitor::poll);
}

MultiSourceMonitor::~MultiSourceMonitor() { shutdown(); }

void MultiSourceMonitor::setRepository(orbita::stand::ParameterRepository* repository)
{
    shutdown();
    repository_ = repository;
    if (repository_) timer_->start();
}

void MultiSourceMonitor::setBindings(std::vector<MonitoredBinding> bindings)
{
    bindings_ = std::move(bindings);
    table_->setRowCount(static_cast<int>(bindings_.size()));
    for (int row = 0; row < table_->rowCount(); ++row) {
        for (int col = 0; col < table_->columnCount(); ++col)
            if (!table_->item(row, col)) table_->setItem(row, col, new QTableWidgetItem);
        table_->item(row, 0)->setText(bindings_[row].sourceTitle);
        table_->item(row, 1)->setText(bindings_[row].parameterTitle);
        table_->item(row, 1)->setToolTip(bindings_[row].parameterKey);
    }
    display();
}

void MultiSourceMonitor::shutdown()
{
    timer_->stop();
    for (auto* worker : workers_) worker->waitForFinished();
    qDeleteAll(workers_);
    workers_.clear();
    repository_ = nullptr;
}

void MultiSourceMonitor::poll()
{
    if (!repository_) return;
    // У каждого источника свой worker: медленный или отключённый прибор не
    // задерживает обновление остальных и не блокирует поток интерфейса.
    QMap<QString, QStringList> keys;
    for (const auto& binding : bindings_)
        if (binding.unavailableReason.isEmpty() && !binding.parameterKey.isEmpty()
            && !keys[binding.sourceId].contains(binding.parameterKey))
            keys[binding.sourceId].push_back(binding.parameterKey);
    for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
        auto*& watcher = workers_[it.key()];
        if (!watcher) {
            watcher = new QFutureWatcher<void>(this);
            connect(watcher, &QFutureWatcherBase::finished, this, &MultiSourceMonitor::display);
        }
        if (watcher->isRunning()) continue;
        auto* repository = repository_;
        const auto source = it.key().toStdString();
        const auto parameterKeys = it.value();
        watcher->setFuture(QtConcurrent::run([repository, source, parameterKeys] {
            for (const auto& key : parameterKeys) {
                try { repository->refresh(source, key.toStdString()); }
                catch (const std::exception&) { /* Состояние показывается по снимку. */ }
            }
        }));
    }
    display();
}

void MultiSourceMonitor::display()
{
    using orbita::stand::SampleQuality;
    for (int row = 0; row < static_cast<int>(bindings_.size()); ++row) {
        QString value = QStringLiteral("—"), unit, status = QStringLiteral("Нет данных"), diagnostic;
        if (!bindings_[row].unavailableReason.isEmpty()) {
            status = QStringLiteral("Недоступен");
            diagnostic = bindings_[row].unavailableReason;
        } else if (repository_ && !bindings_[row].parameterKey.isEmpty()) {
            try {
                const auto sample = repository_->latest(bindings_[row].sourceId.toStdString(),
                    bindings_[row].parameterKey.toStdString(), std::chrono::seconds(5));
                if (sample) {
                    diagnostic = QString::fromStdString(sample->diagnostic);
                    switch (sample->quality) {
                    case SampleQuality::Good:
                        value = QString::number(sample->physicalValue, 'g', 12);
                        unit = QString::fromStdString(sample->unit);
                        status = QStringLiteral("Актуально"); break;
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
    }
}
