#pragma once

#include "orbita_stand/parameter_repository.h"
#include <QFutureWatcher>
#include <QMap>
#include <QObject>
#include <QStringList>

class QTimer;

// Опрос живёт в прикладном слое, отдельно от экранов. Наблюдение и сценарии
// получают результаты из одного хранилища, не открывая повторно прибор.
class ParameterAcquisition final : public QObject {
    Q_OBJECT
public:
    explicit ParameterAcquisition(QObject* parent = nullptr);
    ~ParameterAcquisition() override;
    void setRepository(orbita::stand::ParameterRepository* repository);
    void setSubscriptions(QMap<QString, QStringList> subscriptions);
    void setScenarioSubscriptions(QMap<QString, QStringList> subscriptions);
    void shutdown();
private:
    void poll();
    QTimer* timer_ = nullptr;
    orbita::stand::ParameterRepository* repository_ = nullptr;
    QMap<QString, QStringList> subscriptions_;
    QMap<QString, QStringList> scenarioSubscriptions_;
    QMap<QString, QFutureWatcher<void>*> workers_;
};
