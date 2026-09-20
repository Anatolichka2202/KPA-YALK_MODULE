#pragma once

#include "backend/scenario_engine.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <string>

class QThread;
class TestPage;

namespace tu::hardware { class StandHardware; }

class TuController final : public QObject
{
public:
    explicit TuController(TestPage* page, QObject* parent = nullptr);
    ~TuController() override;

    void registerProcedure(std::string id, tu::ProcedureFunction procedure);

private:
    void loadScenario();
    void loadHardware();
    void registerBuiltInProcedures();
    void refreshBackendReadiness();
    void checkBackendReadiness();
    void startRun(const QString& scenarioCode, const QString& objectSerial,
                  bool allowPartial);

    TestPage* page_ = nullptr;
    tu::ScenarioEngine engine_;
    tu::ScenarioDefinition scenario_;
    std::shared_ptr<tu::hardware::StandHardware> hardware_;
    QThread* runThread_ = nullptr;
    bool scenarioLoaded_ = false;
    bool hardwareLoaded_ = false;
    bool hardwareChecked_ = false;
    bool proceduresReady_ = false;
    QString scenarioError_;
    QString hardwareError_;
};
