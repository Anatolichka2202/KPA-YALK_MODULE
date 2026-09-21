#pragma once

#include "backend/scenario_engine.h"

#include <QObject>
#include <QString>

#include <functional>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class QThread;
class TestPage;
class RunJournalOverlay;

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
    RunJournalOverlay* journal_ = nullptr;
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

    // Scenario lifecycle and reference204 telemetry are intentionally separate.
    // The receiver thread publishes live YALK frames, while these values only
    // provide the current TU node and the validated 97/99 calibration needed to
    // convert raw codes to volts for HMI display.
    std::mutex liveStateMutex_;
    std::string liveNode_;
    double yalkZeroCode_ = 0.0;
    double yalkFullCode_ = 0.0;
    double yalkFullVoltage_ = 6.2;
    bool yalkCalibrationValid_ = false;
    // Последние кадры ROKT для живого min/max на графиках ЯЛК. Это не
    // приёмочные samples процедуры, а видимый оператору разброс телеметрии.
    std::vector<std::deque<double>> yalkLiveWindow_;
};
