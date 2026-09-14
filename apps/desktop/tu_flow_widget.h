#pragma once

#include <QHash>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

class TuFlowWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TuFlowWidget(QWidget* parent = nullptr);

    void setRegisteredSerials(const QStringList& serials);
    void setScenarioAvailable(bool available, const QString& detail = {});
    void beginStandCheck(const QString& serial, const QStringList& requiredEquipment);
    void setEquipmentChecking(const QString& code);
    void setEquipmentStatus(const QString& code, bool ready, const QString& detail = {});
    void resetToSelection();

    QString activeSerial() const;

signals:
    void homeRequested();
    void serialChosen(const QString& serial);
    void startRequested(const QString& serial);
    void retryRequested(const QString& serial);

private:
    void chooseSerial(const QString& serial);
    void updateReadiness();
    void showReady();
    void showNotReady(const QString& detail);

    QStackedWidget* pages_ = nullptr;
    QWidget* selectionPage_ = nullptr;
    QWidget* readinessPage_ = nullptr;
    QComboBox* registered_ = nullptr;
    QLineEdit* manualSerial_ = nullptr;
    QPushButton* manualContinue_ = nullptr;
    QLabel* scenarioState_ = nullptr;
    QLabel* serialTitle_ = nullptr;
    QLabel* readinessState_ = nullptr;
    QLabel* failureDetail_ = nullptr;
    QPushButton* start_ = nullptr;
    QPushButton* retry_ = nullptr;
    QPushButton* back_ = nullptr;

    QString activeSerial_;
    QStringList requiredEquipment_;
    QHash<QString, int> equipmentState_; // -1 checking/unknown, 0 failed, 1 ready
    QHash<QString, QString> equipmentDetail_;
    bool scenarioAvailable_ = true;
};
