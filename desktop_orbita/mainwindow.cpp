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

    dbProvider_ = std::make_unique<MetadataService>(this);
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

        // Активный набор — единый источник истины
        watchSetWidget_ = new WatchSetWidget(dbProvider_.get(), this);
        QDockWidget* wsDock = new QDockWidget("Активный набор", this);
        wsDock->setWidget(watchSetWidget_);
        addDockWidget(Qt::RightDockWidgetArea, wsDock);

        connect(configWidget_, &ConfigManagerWidget::applyConfigRequested,
                this, &MainWindow::applyConfiguration);
        connect(configWidget_, &ConfigManagerWidget::refreshMetadataRequested,
                this, &MainWindow::onRefreshMetadata);

        // Библиотека -> активный набор (добавление на лету)
        connect(paramBrowser, &ParameterBrowser::parametersSelected,
                [this](const QList<QString>& addresses, const QList<QString>& names) {
                    std::vector<orbita::ChannelSpec> specs;
                    for (int i = 0; i < addresses.size(); ++i) {
                        std::string norm = encoding::normalizeAddress(addresses[i].toStdString());
                        if (norm.empty()) continue;
                        QString cat = dbProvider_ ? dbProvider_->getCategory(addresses[i]) : QString();
                        specs.push_back(orbita::ChannelSpec{
                            norm, names[i].toStdString(), cat.toStdString()});
                    }
                    if (watchSetWidget_) watchSetWidget_->addParams(specs);
                });

        // Активный набор -> ядро (горячая замена)
        connect(watchSetWidget_, &WatchSetWidget::watchSetChanged,
                this, &MainWindow::onWatchSetChanged);
        connect(watchSetWidget_, &WatchSetWidget::configSaved,
                [this]() { if (configWidget_) configWidget_->refreshFileList(); });
    }

    connect(updateTimer_, &QTimer::timeout, this, &MainWindow::updateData);
    updateTimer_->start(100);

    // Инициализация устройства не должна валить приложение: если E20-10 не
    // найден (нет DLL/железа), работаем в режиме без устройства.
    try {
        orbita_->setDeviceE2010(0, 10000.0);
        log("Устройство E20-10 найдено");
    } catch (const std::exception& e) {
        orbita_->setDeviceNone();
        log(QString("E20-10 недоступно (%1). Режим без устройства.")
                .arg(QString::fromLocal8Bit(e.what())));
    }
    log("Система инициализирована. Выберите конфигурацию.");
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
        orbita::Snapshot snap = orbita_->getSnapshot();

        // МТВ
        uint32_t sec = snap.mtv_seconds;
        mtvLabel_->setText(QString("%1:%2:%3")
            .arg(sec / 3600,      2, 10, QChar('0'))
            .arg((sec % 3600)/60, 2, 10, QChar('0'))
            .arg(sec % 60,        2, 10, QChar('0')));

        // Данные на столбчатой диаграмме (сырые коды в порядке конфигурации)
        if (!snap.values.empty()) {
            std::vector<double> values;
            values.reserve(snap.values.size());
            for (const auto& v : snap.values)
                values.push_back(v.value);
            plotWidget_->setValues(values, currentChannelNames_);
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

    std::vector<orbita::ChannelSpec> specs;
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        // Пустые строки — визуальные разделители параметров в файле, пропускаем.
        // normalizeAddress сам обрежет комментарий после адреса и приведёт регистр.
        std::string normAddr = encoding::normalizeAddress(line);
        if (normAddr.empty()) continue;
        QString qnorm = QString::fromStdString(normAddr);
        QString name  = dbProvider_ ? dbProvider_->getName(qnorm)     : QString();
        QString cat   = dbProvider_ ? dbProvider_->getCategory(qnorm) : QString();
        specs.push_back(orbita::ChannelSpec{normAddr, name.toStdString(), cat.toStdString()});
    }

    if (specs.empty()) { log("Конфигурация пуста"); return; }

    // Конфиг заменяет активный набор; WatchSet эмитит изменение -> onWatchSetChanged.
    if (watchSetWidget_) watchSetWidget_->setFromSpecs(specs);
    log(QString("Конфигурация загружена: %1 (%2 каналов)")
            .arg(fileName).arg(specs.size()));
}

// ============================================================
//  Активный набор изменился -> горячая замена в ядре
// ============================================================
void MainWindow::onWatchSetChanged(const std::vector<orbita::ChannelSpec>& specs)
{
    try {
        orbita_->setChannels(specs);
    } catch (const std::exception& e) {
        log("Ошибка набора: " + QString::fromLocal8Bit(e.what()));
        return;
    }
    updatePlotLabels();
    plotWidget_->clearAll();
    // Горячая замена: если сбор идёт — кнопки не трогаем. Иначе старт по наличию каналов.
    if (!isRunning_) {
        startBtn_->setEnabled(!specs.empty());
        stopBtn_->setEnabled(false);
    }
}

void MainWindow::updatePlotLabels()
{
    // Имена в том же порядке, что и snap.values (порядок конфигурации).
    std::vector<orbita::ChannelSpec> chans = orbita_->getChannels();
    currentChannelNames_.clear();
    currentChannelNames_.reserve(chans.size());
    for (const auto& c : chans)
        currentChannelNames_.push_back(c.name.empty() ? c.address : c.name);
}

void MainWindow::onRefreshMetadata()
{
    if (dbProvider_) dbProvider_->refresh();
    if (configWidget_) configWidget_->updateMetadata();
    if (paramBrowser)  paramBrowser->rebuildTree();
}
