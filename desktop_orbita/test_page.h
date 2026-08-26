#pragma once

#include <QHash>
#include <QWidget>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTimer;
class TestPlotWidget;

class TestPage final : public QWidget
{
    Q_OBJECT

public:
    explicit TestPage(QWidget* parent = nullptr);

    void setEquipmentStatus(const QString& code, bool ready, const QString& detail);
    void setEquipmentMissingPlugin(const QString& code, const QString& detail);
    void setEquipmentChecking(const QString& code, const QString& detail);

signals:
    void equipmentCheckRequested();

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
    QLabel* readinessLabel_ = nullptr;
    QLabel* diagnosticLabel_ = nullptr;
    QLabel* verdictLabel_ = nullptr;
    QLabel* scopeLabel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QPushButton* checkButton_ = nullptr;
    QPushButton* startButton_ = nullptr;
    TestPlotWidget* plot_ = nullptr;
    QTimer* demoTimer_ = nullptr;
    QHash<QString, EquipmentRow> equipmentRows_;
    int demoStep_ = 0;
};
