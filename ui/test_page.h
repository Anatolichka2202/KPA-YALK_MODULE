#pragma once

#include "model/run_types.h"

#include <QVector>
#include <QWidget>

#include <memory>

class TuFlowWidget;

class TestPage final : public QWidget
{
    Q_OBJECT

public:
    explicit TestPage(QWidget* parent = nullptr);
    ~TestPage() override;

    TuFlowWidget* flow() const;
    QWidget* manualPage() const;
    void showFlow();
    void beginRun(const tu::ScenarioRunResult& result);
    void setStage(int index, const QString& title, const QString& graphTitle,
                  const QString& unit = {}, const QString& first = {}, const QString& second = {});
    void setStageResult(int index, const tu::StepRunResult& result);
    void setProgress(const QString& detail, bool pointCanStart, bool canForceOk = false);
    void setRetryAvailable(bool available);
    void setBbPrSignalAvailable(bool available);
    void setSeries(const QVector<double>& first, const QVector<double>& second = {});
    void setVoltage(const QString& before, const QString& after, const QString& error);
    void appendRunEvent(const tu::RunEvent& event);
    void finishRun(const tu::ScenarioRunResult& result);

signals:
    void stopRequested();
    void skipCurrentStepRequested(); // Ctrl+Shift+Q now requests verdict override, not a skip.
    void retryStageRequested();
    void continueStageRequested();
    void bbPrSignalRequested();
    void pointReady();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
