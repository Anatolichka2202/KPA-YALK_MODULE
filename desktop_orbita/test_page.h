#pragma once

#include <QHash>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <map>
#include <string>

#include "orbita_stand/scenario.h"

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QLineEdit;
class QCheckBox;
class QTableWidget;
class QTimer;
class TestPlotWidget;
class EquipmentControlWidget;

class TestPage final : public QWidget
{
    Q_OBJECT

public:
    explicit TestPage(QWidget* parent = nullptr);
    using EquipmentInvoke = std::function<std::string(
        const std::string&, const std::string&,
        const std::map<std::string, std::string>&)>;
    void setEquipmentInvoker(EquipmentInvoke invoke);

    void setEquipmentStatus(const QString& code, bool ready, const QString& detail);
    void setEquipmentMissingPlugin(const QString& code, const QString& detail);
    void setEquipmentChecking(const QString& code, const QString& detail);
    void setScenarioInfo(const QString& code, bool available, bool diagnostic,
                         const QStringList& requiredEquipment,
                         const QString& detail);
    void setEngineerMode(bool enabled);
    void setRunInProgress(bool running, const QString& stage = {});
    void setRunEvent(const orbita::stand::RunEvent& event);
    void setRunResult(const orbita::stand::ScenarioRunResult& result,
                      const QString& tuReportPath = {},
                      const QString& productionReportPath = {});
    QString currentScenarioCode() const;

signals:
    void equipmentCheckRequested();
    void runRequested(const QString& scenarioCode, const QString& objectSerial,
                      bool allowPartial);
    void stopRequested();

private slots:
    void updateStartAvailability();
    void rebuildScopes();
    void rebuildTests();
    void updateSelectionSummary();
    void startSelectedTest();
    void advanceDemo();

private:
    struct EquipmentRow {
        int row = -1;
        bool ready = false;
        bool operatorConfirmation = false;
    };
    struct ScenarioInfo {
        bool available = false;
        bool diagnostic = false;
        QStringList requiredEquipment;
        QString detail;
    };

    void addEquipment(const QString& code, const QString& name,
                      const QString& connection, const QString& initialDetail,
                      bool operatorConfirmation = false);
    QStringList requiredEquipment() const;
    QString selectedObjectCode() const;
    QString selectedScopeCode() const;
    QString selectedTestCode() const;
    void resetResults();
    void finishDemo();

    QComboBox* objectCombo_ = nullptr;
    QComboBox* scopeCombo_ = nullptr;
    QComboBox* testCombo_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QTableWidget* equipmentTable_ = nullptr;
    QTableWidget* resultTable_ = nullptr;
    QTableWidget* summaryTable_ = nullptr;
    QLabel* readinessLabel_ = nullptr;
    QLabel* diagnosticLabel_ = nullptr;
    QLabel* verdictLabel_ = nullptr;
    QLabel* scopeLabel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QPushButton* checkButton_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* detailsButton_ = nullptr;
    QPushButton* tuReportButton_ = nullptr;
    QPushButton* productionReportButton_ = nullptr;
    QLineEdit* serialEdit_ = nullptr;
    QCheckBox* partialCheck_ = nullptr;
    TestPlotWidget* plot_ = nullptr;
    EquipmentControlWidget* advancedControl_ = nullptr;
    QWidget* advancedContainer_ = nullptr;
    EquipmentInvoke equipmentInvoke_;
    QTimer* demoTimer_ = nullptr;
    QHash<QString, EquipmentRow> equipmentRows_;
    QHash<QString, ScenarioInfo> scenarios_;
    int demoStep_ = 0;
    bool runInProgress_ = false;
    bool engineerMode_ = false;
    int completedSteps_ = 0;
    QString tuReportPath_;
    QString productionReportPath_;
};
