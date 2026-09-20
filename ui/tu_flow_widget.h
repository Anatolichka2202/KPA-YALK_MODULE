#pragma once

#include <QHash>
#include <QStringList>
#include <QWidget>

#include "model/run_types.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;

class TuFlowWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TuFlowWidget(QWidget* parent = nullptr);

    void setRegisteredSerials(const QStringList& serials);
    // Compatibility hook. In the v0.5 TU flow the operator is intentionally
    // entered only after the automatic run, immediately before the report.
    void setOperators(const QStringList& operators);
    void setScenarioAvailable(bool available, const QString& detail = {});
    void beginStandCheck(const QString& serial, const QString& operatorName,
                         const QStringList& requiredEquipment);
    void setEquipmentChecking(const QString& code);
    void setEquipmentStatus(const QString& code, bool ready, const QString& detail = {});
    void completeRun(const tu::ScenarioRunResult& result,
                     const QString& tuReportPath);
    void setYvpBypassActive(bool active);
    void resetToSelection();

    QString activeSerial() const;
    QString activeOperator() const;

signals:
    void homeRequested();
    // operatorName remains in the signal signature for binary/source stability;
    // TU v0.5 emits an empty value until the post-run report step.
    void readinessRequested(const QString& serial, const QString& operatorName);
    void startRequested(const QString& serial, const QString& operatorName);
    void retryRequested(const QString& serial, const QString& operatorName);
    void yvpBypassRequested();

private:
    QString selectedSerial() const;
    void updateSelectionAvailability();
    void updateReadiness();
    void showReady();
    void showNotReady(const QString& detail);
    void setSerialMode(bool manual);
    void showOperatorEntry();
    void showReport();
    void populateReportRows(const tu::ScenarioRunResult& result);
    void applyOperatorToTuProtocol(const QString& operatorName);

    QStackedWidget* pages_ = nullptr;
    QWidget* selectionPage_ = nullptr;
    QWidget* readinessPage_ = nullptr;
    QWidget* operatorPage_ = nullptr;
    QWidget* reportPage_ = nullptr;
    QComboBox* registered_ = nullptr;
    QLineEdit* manualSerial_ = nullptr;
    QPushButton* useRegistered_ = nullptr;
    QPushButton* useManual_ = nullptr;
    QPushButton* check_ = nullptr;
    QLabel* scenarioState_ = nullptr;
    QLabel* serialTitle_ = nullptr;
    QLabel* readinessState_ = nullptr;
    QLabel* failureDetail_ = nullptr;
    QPushButton* start_ = nullptr;
    QPushButton* retry_ = nullptr;
    QPushButton* back_ = nullptr;
    QLineEdit* completionOperator_ = nullptr;
    QPushButton* buildReport_ = nullptr;
    QLabel* reportSerial_ = nullptr;
    QLabel* reportOperator_ = nullptr;
    QLabel* reportDate_ = nullptr;
    QLabel* reportVerdict_ = nullptr;
    QLabel* reportPaths_ = nullptr;
    QTableWidget* reportTable_ = nullptr;
    QPushButton* openReport_ = nullptr;

    QString activeSerial_;
    QString activeOperator_;
    QString finalVerdict_;
    QString finalReportPaths_;
    QString reportPath_;
    QStringList requiredEquipment_;
    QHash<QString, int> equipmentState_; // -1 checking/unknown, 0 failed, 1 ready
    QHash<QString, QString> equipmentDetail_;
    bool scenarioAvailable_ = true;
    bool manualMode_ = false;
    bool runStarted_ = false;
    bool completionShown_ = false;
    bool isReady_ = false;
    bool isNotReady_ = false;
};
