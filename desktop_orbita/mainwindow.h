#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QTextEdit>
#include <QSpinBox>

#include "orbita.h"
#include "metadata_service.h"
#include "parameter_browser.h"
#include "config_manager_widget.h"

class PlotWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onStart();
    void onStop();
    void onToggleRecording();
    void updateData();
    void onRefreshMetadata();
    void applyConfiguration(const QString& fileName);

private:
    void setupUi();
    void updatePlotLabels();
    void log(const QString& msg);
    QString nextRecordingPath() const;

    std::unique_ptr<orbita::Orbita> orbita_;
    std::unique_ptr<MetadataService> dbProvider_;
    QTimer*        updateTimer_;
    QElapsedTimer  elapsedTimer_;
    bool           isRunning_   = false;
    bool           isRecording_ = false;

    QLabel*      mtvLabel_;
    PlotWidget*  plotWidget_;
    QPushButton* startBtn_;
    QPushButton* stopBtn_;
    QPushButton* recordBtn_;
    QLabel*      recordingLabel_;
    QSpinBox*    windowSpin_;
    QCheckBox*   invertCheck_;
    QTextEdit*   logEdit_;

    ConfigManagerWidget* configWidget_;
    ParameterBrowser*    paramBrowser;

    std::vector<std::string> currentChannelNames_;
};

#endif // MAINWINDOW_H
