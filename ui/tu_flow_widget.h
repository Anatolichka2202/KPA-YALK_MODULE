#pragma once

#include <QHash>
#include <QStringList>
#include <QWidget>

#include "model/run_types.h"

class QLabel;
class QComboBox;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;

class TuFlowWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TuFlowWidget(QWidget* parent = nullptr);

    void setScenarioAvailable(bool available, const QString& detail = {});
    void beginStandCheck(const QString& serial, const QStringList& requiredEquipment);
    void setEquipmentStatus(const QString& code, bool ready, const QString& detail = {});
    void completeRun(const tu::ScenarioRunResult& result);
    void resetToSelection();
    void setRs485Selection(bool visible);
    QString rs485Port() const;

signals:
    void manualRequested();
    void readinessRequested(const QString& serial);
    void startRequested(const QString& serial);

private:
    QString selectedSerial() const;
    void updateSelectionAvailability();
    void updateReadiness();
    void showReady();
    void showNotReady(const QString& detail);
    void showOperatorEntry();
    void showReport();
    void populateReportRows(const tu::ScenarioRunResult& result);

    QStackedWidget* pages_ = nullptr;
    QWidget* selectionPage_ = nullptr;
    QWidget* readinessPage_ = nullptr;
    QWidget* operatorPage_ = nullptr;
    QWidget* reportPage_ = nullptr;
    QComboBox* rs485_ = nullptr;
    QLabel* rs485Label_ = nullptr;
    QLineEdit* manualSerial_ = nullptr;
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
    QLabel* reportError_ = nullptr;
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
    tu::ScenarioRunResult result_;
    QStringList requiredEquipment_;
    QHash<QString, int> equipmentState_; // -1 checking/unknown, 0 failed, 1 ready
    QHash<QString, QString> equipmentDetail_;
    bool scenarioAvailable_ = true;
    bool isReady_ = false;
    bool isNotReady_ = false;
};
