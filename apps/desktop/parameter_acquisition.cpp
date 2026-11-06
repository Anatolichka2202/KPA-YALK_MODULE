#include "parameter_acquisition.h"
#include <QTimer>
#include <QtConcurrentRun>

ParameterAcquisition::ParameterAcquisition(QObject* parent) : QObject(parent), timer_(new QTimer(this))
{
    timer_->setInterval(250);
    connect(timer_, &QTimer::timeout, this, &ParameterAcquisition::poll);
}
ParameterAcquisition::~ParameterAcquisition() { shutdown(); }

void ParameterAcquisition::setRepository(orbita::stand::ParameterRepository* repository)
{
    shutdown();
    repository_ = repository;
    if (repository_) timer_->start();
}
void ParameterAcquisition::setSubscriptions(QMap<QString, QStringList> subscriptions)
{
    subscriptions_ = std::move(subscriptions);
}
void ParameterAcquisition::setScenarioSubscriptions(QMap<QString, QStringList> subscriptions)
{
    scenarioSubscriptions_ = std::move(subscriptions);
}
void ParameterAcquisition::shutdown()
{
    timer_->stop();
    for (auto* worker : workers_) worker->waitForFinished();
    qDeleteAll(workers_);
    workers_.clear();
    repository_ = nullptr;
    scenarioSubscriptions_.clear();
}
void ParameterAcquisition::poll()
{
    if (!repository_) return;
    // Независимые задачи не позволяют одному медленному прибору задержать
    // остальные источники или поток интерфейса. Сам источник сериализует
    // свои вызовы в ParameterRepository.
    auto active = subscriptions_;
    for (auto it = scenarioSubscriptions_.cbegin(); it != scenarioSubscriptions_.cend(); ++it)
        for (const auto& key : it.value())
            if (!active[it.key()].contains(key)) active[it.key()].push_back(key);
    for (auto it = active.cbegin(); it != active.cend(); ++it) {
        auto*& watcher = workers_[it.key()];
        if (!watcher) watcher = new QFutureWatcher<void>(this);
        if (watcher->isRunning()) continue;
        auto* repository = repository_;
        const auto source = it.key().toStdString();
        const auto keys = it.value();
        watcher->setFuture(QtConcurrent::run([repository, source, keys] {
            for (const auto& key : keys) {
                try { repository->refresh(source, key.toStdString()); }
                catch (const std::exception&) { /* Читатель видит состояние хранилища. */ }
            }
        }));
    }
}
