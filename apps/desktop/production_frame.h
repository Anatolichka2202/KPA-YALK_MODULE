#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QString>
#include <functional>

class ProductionFrame : public QWidget
{
    Q_OBJECT

public:
    explicit ProductionFrame(QWidget* parent = nullptr);

    void updateHeader(const QString& serial, const QString& scope, const QString& operatorName);
    void updateTelemetry(const QString& elapsed, const QString& progressLabel, const QString& progressText, double fraction);
    void updateOperation(const QString& label, const QString& value);
    void setStopCallback(std::function<void()> callback);

    QWidget* getWorkspace() { return workspace_; }

private:
    void setupUi();

    // Header
    QLabel* serialLabel_;
    QLabel* scopeLabel_;
    QLabel* operatorLabel_;
    QPushButton* stopButton_;

    // Workspace
    QWidget* workspace_;
    QWidget* sidebar_;

    // Telemetry
    QLabel* elapsedLabel_;
    QLabel* progressLabel_;
    QLabel* progressText_;
    QProgressBar* progressBar_;

    std::function<void()> stopCallback_;
};
