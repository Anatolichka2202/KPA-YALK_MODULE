#include "mainwindow.h"
#include "plot_widget.h"
#include "config_manager_widget.h"
#include "parameter_browser.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QFileDialog>
#include <QCheckBox>
#include <QGroupBox>
#include <QMessageBox>
#include <QTextEdit>
#include <QSpinBox>
#include <QDebug>
#include <QDockWidget>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <sstream>
#include "encoding_utils.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , orbita_(std::make_unique<orbita::Orbita>())
    , updateTimer_(new QTimer(this))
    , configWidget_(nullptr)
    , paramBrowser(nullptr)
{
    setupUi();

    dbProvider_ = std::make_unique<ParameterDatabase>(this);
    if (!dbProvider_->open()) {
        log("Ошибка открытия БД parameters.db");
    } else {
        configWidget_ = new ConfigManagerWidget(dbProvider_.get(), this);
        paramBrowser  = new ParameterBrowser(dbProvider_.get(), this);

        QDockWidget* cfgDock = new QDockWidget("Конфигурации", this);
        cfgDock->setWidget(configWidget_);
        addDockWidget(Qt::LeftDockWidgetArea, cfgDock);

        QDockWidget* dbDock = new QDockWidget("Библиотека параметров", this);
        dbDock->setWidget(paramBrowser);
        addDockWidget(Qt::RightDockWidgetArea, dbDock);

        connect(configWidget_, &ConfigManagerWidget::applyConfigRequested,
                this, &MainWindow::applyConfiguration);
        connect(configWidget_, &ConfigManagerWidget::refreshMetadataRequested,
                this, &MainWindow::onRefreshMetadata);
        connect(paramBrowser, &ParameterBrowser::parametersSelected,
                [this](const QList<QString>& addresses, const QList<QString>& names) {
                    Q_UNUSED(addresses); Q_UNUSED(names);
                    // TODO: добавление каналов из браузера параметров
                });
    }

    connect(updateTimer_, &QTimer::timeout, this, &MainWindow::updateData);
    updateTimer_->start(100);

    orbita_->setDeviceE2010(0, 10000.0);
    log("Система инициализирована. Загрузите конфигурацию адресов.");
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    setWindowTitle("Орбита-IV — Телеметрия");
    resize(1200, 700);

    auto* central    = new QWidget;
    auto* mainLayout = new QVBoxLayout(central);

    // ---- Панель управления ----
    auto* controlGroup  = new QGroupBox("Управление");
    auto* controlLayout = new QHBoxLayout;

    startBtn_    = new QPushButton("▶ Старт");
    stopBtn_     = new QPushButton("■ Стоп");
    invertCheck_ = new QCheckBox("Инверт.");

    startBtn_->setEnabled(false);
    stopBtn_->setEnabled(false);

    controlLayout->addWidget(startBtn_);
    controlLayout->addWidget(stopBtn_);
    controlLayout->addWidget(invertCheck_);
    controlLayout->addStretch();
    controlGroup->setLayout(controlLayout);
    mainLayout->addWidget(controlGroup);

    // ---- Панель записи и настроек графика ----
    auto* recGroup  = new QGroupBox("Запись и отображение");
    auto* recLayout = new QHBoxLayout;

    recordBtn_      = new QPushButton("⏺ Запись");
    recordBtn_->setEnabled(false);
    recordingLabel_ = new QLabel("—");
    recordingLabel_->setMinimumWidth(180);

    auto* winLabel = new QLabel("Окно:");
    windowSpin_    = new QSpinBox;
    windowSpin_->setRange(5, 300);
    windowSpin_->setValue(30);
    windowSpin_->setSuffix(" с");

    recLayout->addWidget(recordBtn_);
    recLayout->addWidget(recordingLabel_);
    recLayout->addStretch();
    recLayout->addWidget(winLabel);
    recLayout->addWidget(windowSpin_);
    recGroup->setLayout(recLayout);
    mainLayout->addWidget(recGroup);

    // ---- График ----
    plotWidget_ = new PlotWidget(this);
    plotWidget_->setMinimumHeight(380);
    mainLayout->addWidget(plotWidget_, 1);

    // ---- Статус-бар бортового времени ----
    auto* infoGroup  = new QGroupBox("МТВ (бортовое время)");
    auto* infoLayout = new QHBoxLayout;
    mtvLabel_ = new QLabel("--:--:--");
    mtvLabel_->setFont(QFont("Courier New", 14, QFont::Bold));
    infoLayout->addWidget(mtvLabel_);
    infoLayout->addStretch();
    infoGroup->setLayout(infoLayout);
    mainLayout->addWidget(infoGroup);

    // ---- Лог ----
    logEdit_ = new QTextEdit;
    logEdit_->setMaximumHeight(120);
    logEdit_->setReadOnly(true);
    logEdit_->setFont(QFont("Courier New", 9));
    mainLayout->addWidget(logEdit_);

    setCentralWidget(central);

    // ---- Сигналы ----
    connect(startBtn_,  &QPushButton::clicked, this, &MainWindow::onStart);
    connect(stopBtn_,   &QPushButton::clicked, this, &MainWindow::onStop);
    connect(recordBtn_, &QPushButton::clicked, this, &MainWindow::onToggleRecording);
    connect(invertCheck_, &QCheckBox::toggled,
            [this](bool v){ orbita_->setInvertSignal(v); });
    connect(windowSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int v){ plotWidget_->setWindowSeconds(v); });
}

void MainWindow::log(const QString& msg)
{
    QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    logEdit_->append(QString("[%1] %2").arg(ts, msg));
    qDebug() << msg;
}

// ============================================================
//  Старт / Стоп аппаратного сбора
// ============================================================
void MainWindow::onStart()
{
    try {
        orbita_->start();
        elapsedTimer_.restart();
        isRunning_ = true;
        plotWidget_->clearAll();
        startBtn_->setEnabled(false);
        stopBtn_->setEnabled(true);
        recordBtn_->setEnabled(true);
        log("Старт сбора данных (E20-10)");
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Ошибка запуска", e.what());
    }
}

void MainWindow::onStop()
{
    orbita_->stop();
    isRunning_ = false;
    if (isRecording_) onToggleRecording();  // остановить запись
    startBtn_->setEnabled(true);
    stopBtn_->setEnabled(false);
    recordBtn_->setEnabled(false);
    log("Стоп сбора данных");
}

// ============================================================
//  Запись TLM
// ============================================================
QString MainWindow::nextRecordingPath() const
{
    QString dir = QCoreApplication::applicationDirPath() + "/records";
    QDir().mkpath(dir);
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    return dir + "/" + ts + ".tlm";
}

void MainWindow::onToggleRecording()
{
    if (!isRecording_) {
        QString path = nextRecordingPath();
        orbita_->startRecording(path.toStdString());
        isRecording_ = true;
        recordBtn_->setText("⏹ Стоп запись");
        recordingLabel_->setText("● " + path.section('/', -1));
        recordingLabel_->setStyleSheet("color: red; font-weight: bold");
        log("Запись: " + path);
    } else {
        orbita_->stopRecording();
        isRecording_ = false;
        recordBtn_->setText("⏺ Запись");
        recordingLabel_->setText("—");
        recordingLabel_->setStyleSheet("");
        log("Запись остановлена");
    }
}

// ============================================================
//  Периодическое обновление графика
// ============================================================
void MainWindow::updateData()
{
    if (!isRunning_) return;

    if (orbita_->waitForData(std::chrono::milliseconds(50))) {
        // МТВ
        uint32_t sec = orbita_->getCurrentTimeSeconds();
        mtvLabel_->setText(QString("%1:%2:%3")
            .arg(sec / 3600,      2, 10, QChar('0'))
            .arg((sec % 3600)/60, 2, 10, QChar('0'))
            .arg(sec % 60,        2, 10, QChar('0')));

        // Данные на графике
        int n = orbita_->getAnalogCount();
        if (n > 0) {
            double t = elapsedTimer_.elapsed() / 1000.0;
            std::vector<double> values(n);
            for (int i = 0; i < n; ++i)
                values[i] = orbita_->getAnalog(i);
            plotWidget_->addSamples(t, values, currentChannelNames_);
        }
    }
}

// ============================================================
//  Применение конфигурации из ConfigManagerWidget
// ============================================================
void MainWindow::applyConfiguration(const QString& fileName)
{
    if (fileName.isEmpty()) return;
    QString fullPath = QCoreApplication::applicationDirPath() + "/address/" + fileName;
    std::string content = encoding::readFileToUtf8(fullPath.toStdString());
    if (content.empty()) {
        log("Не удалось прочитать: " + fileName);
        return;
    }

    std::vector<std::pair<std::string, std::string>> pairs;
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        std::string normAddr = encoding::normalizeAddress(line);
        if (normAddr.empty()) continue;
        QString qnorm = QString::fromStdString(normAddr);
        QString name  = dbProvider_ ? dbProvider_->getName(qnorm) : QString();
        pairs.emplace_back(normAddr, name.toStdString());
    }

    if (pairs.empty()) { log("Конфигурация пуста"); return; }

    try {
        orbita_->setChannelsFromPairs(pairs);
        log(QString("Конфигурация применена: %1 (%2 каналов)")
                .arg(fileName).arg(pairs.size()));
        updatePlotLabels();
        plotWidget_->clearAll();
        startBtn_->setEnabled(true);
        stopBtn_->setEnabled(false);
    } catch (const std::exception& e) {
        log("Ошибка: " + QString::fromLocal8Bit(e.what()));
    }
}

void MainWindow::updatePlotLabels()
{
    int n = orbita_->getAnalogCount();
    currentChannelNames_.resize(n);
    for (int i = 0; i < n; ++i)
        currentChannelNames_[i] = orbita_->getAnalogChannelName(i);
}

void MainWindow::onRefreshMetadata()
{
    if (dbProvider_) dbProvider_->refresh();
    if (configWidget_) configWidget_->updateMetadata();
    if (paramBrowser)  paramBrowser->rebuildTree();
}
