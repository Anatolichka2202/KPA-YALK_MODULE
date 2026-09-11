#pragma once

#include <QStringList>
#include <QWidget>

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "orbita_stand/scenario.h"

class TestPage final : public QWidget
{
    Q_OBJECT

public:
    explicit TestPage(QWidget* parent = nullptr);
    ~TestPage() override;

    using EquipmentInvoke = std::function<std::string(
        const std::string&, const std::string&,
        const std::map<std::string, std::string>&)>;

    void setEquipmentInvoker(EquipmentInvoke invoke);
    void setEquipmentStatus(const QString& code, bool ready, const QString& detail);
    void setEquipmentConnection(const QString& code, const QString& connection);
    void setEquipmentMissingPlugin(const QString& code, const QString& detail);
    void setEquipmentChecking(const QString& code, const QString& detail);
    void setScenarioInfo(const QString& code, bool available, bool diagnostic,
                         const QStringList& requiredEquipment,
                         const QString& detail);
    void setEngineerMode(bool enabled);
    bool isEngineerMode() const;
    void setProductionMode(bool enabled);
    void setRunInProgress(bool running, const QString& stage = {});
    void setRunEvent(const orbita::stand::RunEvent& event);
    void setRunResult(const orbita::stand::ScenarioRunResult& result,
                      const QString& tuReportPath = {},
                      const QString& productionReportPath = {});
    QString currentScenarioCode() const;
    bool includeYvp() const;
    bool includeProductionOverload() const;
    bool includeProductionSurvival() const;

    void registerEquipmentRow(const QString& code, const QString& name,
                              const QString& connection, const QString& initialDetail,
                              bool operatorConfirmation = false);

signals:
    void homeRequested();
    void equipmentCheckRequested();
    void runRequested(const QString& scenarioCode, const QString& objectSerial,
                      bool allowPartial);
    void stopRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void updateStartAvailability();
    void rebuildScopes();
    void rebuildTests();
    void updateSelectionSummary();
    void startSelectedTest();
    void advanceDemo();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
