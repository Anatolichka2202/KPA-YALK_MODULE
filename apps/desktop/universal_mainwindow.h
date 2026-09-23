#pragma once

#include "ktma_mainwindow.h"
#include "orbita_stand/project.h"
#include "orbita_stand/scenario.h"

#include <QFutureWatcher>

#include <optional>

class GenericCheckDialog;

class UniversalMainWindow final : public KtmaMainWindow
{
    Q_OBJECT

public:
    explicit UniversalMainWindow(QWidget* parent = nullptr);
    ~UniversalMainWindow() override;

private slots:
    void openGenericCheck();
    void reloadGenericScenarios();
    void editGenericScenario(const QString& path);
    void runGenericScenario(const QString& path,
                            const QString& objectSerial,
                            const QString& description,
                            const QString& reportTemplate);
    void stopGenericScenario();
    void finishGenericScenario();

private:
    QString renderGenericReport(const orbita::stand::ScenarioRunResult& result) const;
    QString renderSteps(const std::vector<orbita::stand::StepRunResult>& steps, int level = 0) const;
    QString renderEvents(const std::vector<orbita::stand::RunEvent>& events) const;
    QString formatTimestamp(std::chrono::system_clock::time_point timestamp) const;

    std::optional<orbita::stand::ProjectDefinition> project_;
    GenericCheckDialog* genericDialog_ = nullptr;
    QFutureWatcher<orbita::stand::ScenarioRunResult>* genericWatcher_ = nullptr;
    QString genericDescription_;
    QString genericTemplate_;
};
