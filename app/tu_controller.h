#pragma once

#include "backend/scenario_engine.h"

#include <QObject>

#include <functional>
#include <string>

class QThread;
class TestPage;

class TuController final : public QObject
{
public:
    explicit TuController(TestPage* page, QObject* parent = nullptr);
    ~TuController() override;

    void registerProcedure(std::string id, tu::ProcedureFunction procedure);

private:
    void loadScenario();
    void refreshBackendReadiness();
    void checkBackendReadiness();
    void startRun(const QString& scenarioCode, const QString& objectSerial,
                  bool allowPartial);

    TestPage* page_ = nullptr;
    tu::ScenarioEngine engine_;
    tu::ScenarioDefinition scenario_;
    QThread* runThread_ = nullptr;
    bool scenarioLoaded_ = false;
    bool proceduresReady_ = false;
    QString scenarioError_;
};
