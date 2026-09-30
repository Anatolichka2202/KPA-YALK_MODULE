#pragma once

#include "orbita_stand/scenario.h"

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;

struct GenericScenarioEntry
{
    QString workflowId;
    QString workflowTitle;
    QString title;
    QString id;
    QString version;
    QString path;
    QStringList requiredResources;
    bool resourcesConfigured = true;
};

class GenericCheckDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit GenericCheckDialog(QWidget* parent = nullptr);

    void setScenarios(const QVector<GenericScenarioEntry>& scenarios);
    void setRunning(bool running, const QString& detail = {});
    void appendEvent(const orbita::stand::RunEvent& event);
    void setReportHtml(const QString& html, const QString& summary);

signals:
    void editScenarioRequested(const QString& path);
    void runRequested(const QString& workflowId,
                      const QString& scenarioPath,
                      const QString& objectSerial,
                      const QString& description,
                      const QString& engineeringOverride,
                      const QString& reportTemplate);
    void stopRequested();

private slots:
    void requestRun();
    void editScenario();
    void saveReport();
    void updateResourcePreview();

private:
    QString selectedWorkflowId() const;
    QString selectedScenarioPath() const;
    QString defaultReportTemplate() const;

    QComboBox* scenario_ = nullptr;
    QLabel* resourcePreview_ = nullptr;
    QLineEdit* serial_ = nullptr;
    QPlainTextEdit* description_ = nullptr;
    QPlainTextEdit* engineeringOverride_ = nullptr;
    QPlainTextEdit* reportTemplate_ = nullptr;
    QTextBrowser* eventLog_ = nullptr;
    QTextBrowser* reportPreview_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* editScenarioButton_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* saveReportButton_ = nullptr;
    QString reportHtml_;
};
