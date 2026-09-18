#pragma once

#include <QHash>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;

class TuFlowWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TuFlowWidget(QWidget* parent = nullptr);

    void setRegisteredSerials(const QStringList& serials);
    void setOperators(const QStringList& operators);
    void setScenarioAvailable(bool available, const QString& detail = {});
    void beginStandCheck(const QString& serial, const QString& operatorName,
                         const QStringList& requiredEquipment);
    void setEquipmentChecking(const QString& code);
    void setEquipmentStatus(const QString& code, bool ready, const QString& detail = {});
    void resetToSelection();

    QString activeSerial() const;
    QString activeOperator() const;

signals:
    void homeRequested();
    void readinessRequested(const QString& serial, const QString& operatorName);
    void startRequested(const QString& serial, const QString& operatorName);
    void retryRequested(const QString& serial, const QString& operatorName);

private:
    void updateSelectionAvailability();
    void updateReadiness();
    void showReady();
    void showNotReady(const QString& detail);

    QStackedWidget* pages_ = nullptr;
    QWidget* selectionPage_ = nullptr;
    QWidget* readinessPage_ = nullptr;
    QComboBox* operator_ = nullptr;
    QComboBox* registered_ = nullptr;
    QPushButton* check_ = nullptr;
    QLabel* scenarioState_ = nullptr;
    QLabel* serialTitle_ = nullptr;
    QLabel* readinessState_ = nullptr;
    QLabel* failureDetail_ = nullptr;
    QPushButton* start_ = nullptr;
    QPushButton* retry_ = nullptr;
    QPushButton* back_ = nullptr;

    QString activeSerial_;
    QString activeOperator_;
    QStringList requiredEquipment_;
    QHash<QString, int> equipmentState_; // -1 checking/unknown, 0 failed, 1 ready
    QHash<QString, QString> equipmentDetail_;
    bool scenarioAvailable_ = true;
};
