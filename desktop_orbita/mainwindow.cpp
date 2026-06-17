#include "mainwindow.h"
#include "channel_status.h"
#include "encoding_utils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QIcon>
#include <sstream>
#include <regex>

#define ORBITA_VERSION "0.1.0-alpha"

// ----------------------------------------------------------------------------
//  Конструктор / Деструктор
// ----------------------------------------------------------------------------
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , orbita_(std::make_unique<orbita::Orbita>())
    , updateTimer_(new QTimer(this))
{
    // Порядок важен: тулбар создаёт startBtn_/actMain_ и пр., которые нужны
    // в setupDockWidgets() (там вызывается onWatchSetChanged) и в setMode().
    setupUi();
    setupToolBar();
    setupDockWidgets();

    // Нижняя строка статуса
    m_statusBarLabel = new QLabel(this);
    m_statusBarLabel->setStyleSheet("color:#7e8a98; font-family:'IBM Plex Mono'; font-size:11px;");
    statusBar()->addPermanentWidget(m_statusBarLabel, 1);
    statusBar()->setStyleSheet("QStatusBar{background:#0e1115; border-top:1px solid #232a33;}");
    m_statusBarLabel->setText("● сбор остановлен");

    // Теперь все элементы созданы — можно выставить начальный режим
    setMode(ModeMain);

    // Инициализация устройства
    try {
        orbita_->setDeviceE2010(0, 10000.0);
        log("Устройство E20-10 найдено");
    } catch (const std::exception& e) {
        orbita_->setDeviceNone();
        log(QString("E20-10 недоступно (%1). Режим без устройства.")
                .arg(QString::fromLocal8Bit(e.what())));
    }

    // Таймер обновления
    connect(updateTimer_, &QTimer::timeout, this, &MainWindow::updateData);
    updateTimer_->start(100);

    log("Система инициализирована. Выберите конфигурацию.");
}

MainWindow::~MainWindow() = default;

// ----------------------------------------------------------------------------
//  Инициализация UI
// ----------------------------------------------------------------------------
void MainWindow::setupUi()
{
    setWindowTitle(QString("Орбита-IV — Телеметрия · %1").arg(ORBITA_VERSION));
    resize(1280, 800);

    // Центральный стек
    centralStack_ = new QStackedWidget;
    setCentralWidget(centralStack_);

    // --- Создаём страницы ---
    // Страница "Сбор"
    mainPage_ = new MainPage;
    centralStack_->addWidget(mainPage_);

    // Страница "Детальный просмотр"
    detailView_ = new DetailView;
    centralStack_->addWidget(detailView_);

    // Страница "Конфигуратор" (пока временная, будет заменена после открытия БД)
    QWidget* configPlaceholder = new QWidget;
    configPlaceholder->setStyleSheet("background: #14171c;");
    QVBoxLayout* cfgLayout = new QVBoxLayout(configPlaceholder);
    cfgLayout->addWidget(new QLabel("Загрузка конфигуратора..."));
    cfgLayout->setAlignment(Qt::AlignCenter);
    centralStack_->addWidget(configPlaceholder);

    // Страница "База параметров" (временная)
    QWidget* dbPlaceholder = new QWidget;
    dbPlaceholder->setStyleSheet("background: #14171c;");
    QVBoxLayout* dbLayout = new QVBoxLayout(dbPlaceholder);
    dbLayout->addWidget(new QLabel("Загрузка базы параметров..."));
    dbLayout->setAlignment(Qt::AlignCenter);
    centralStack_->addWidget(dbPlaceholder);
    // Начальный режим выставляется в конце конструктора — после setupToolBar()/setupDockWidgets(),
    // т.к. setMode() обращается к actMain_ и докам, которых здесь ещё нет.

    // --- Лог (нижняя панель) ---
    logEdit_ = new QTextEdit;
    logEdit_->setMaximumHeight(120);
    logEdit_->setReadOnly(true);
    logEdit_->setFont(QFont("Courier New", 9));
    logEdit_->setStyleSheet("QTextEdit { background: #0e1115; color: #aab4c0; border: 1px solid #2a313b; }");
}

void MainWindow::setupDockWidgets()
{
    // Открываем БД
    dbProvider_ = std::make_unique<MetadataService>(this);
    if (!dbProvider_->open()) {
        log("Ошибка открытия БД parameters.db");
        // Создаём заглушки для док-виджетов, чтобы приложение не падало
        QWidget* dummy = new QWidget;
        dummy->setStyleSheet("background: #14171c;");
        QLabel* errLabel = new QLabel("БД не загружена");
        errLabel->setStyleSheet("color: #cf5b52;");
        QVBoxLayout* l = new QVBoxLayout(dummy);
        l->addWidget(errLabel);
        l->setAlignment(Qt::AlignCenter);

        configDock_ = new QDockWidget("Конфигурации", this);
        configDock_->setWidget(dummy);
        addDockWidget(Qt::LeftDockWidgetArea, configDock_);

        paramDock_ = new QDockWidget("Библиотека параметров", this);
        paramDock_->setWidget(new QWidget);
        addDockWidget(Qt::RightDockWidgetArea, paramDock_);

        watchSetDock_ = new QDockWidget("Активный набор", this);
        watchSetDock_->setWidget(new QWidget);
        addDockWidget(Qt::RightDockWidgetArea, watchSetDock_);

        // Заменяем плейсхолдеры Конфиг и БД в центральном стеке на страницы с ошибкой
        QWidget* oldConfig = centralStack_->widget(ModeConfig);
        QWidget* oldDb = centralStack_->widget(ModeDb);
        centralStack_->removeWidget(oldConfig);
        centralStack_->removeWidget(oldDb);
        delete oldConfig;
        delete oldDb;

        // Страница "Конфиг" с сообщением об ошибке
        QWidget* configError = new QWidget;
        configError->setStyleSheet("background: #14171c;");
        QVBoxLayout* cfgErrLayout = new QVBoxLayout(configError);
        QLabel* cfgErrLabel = new QLabel("База параметров недоступна\n(parameters.db не найдена)");
        cfgErrLabel->setStyleSheet("color: #cf5b52; font-size: 13px;");
        cfgErrLayout->addWidget(cfgErrLabel);
        cfgErrLayout->setAlignment(Qt::AlignCenter);
        centralStack_->insertWidget(ModeConfig, configError);

        // Страница "БД" с сообщением об ошибке
        QWidget* dbError = new QWidget;
        dbError->setStyleSheet("background: #14171c;");
        QVBoxLayout* dbErrLayout = new QVBoxLayout(dbError);
        QLabel* dbErrLabel = new QLabel("База параметров недоступна\n(parameters.db не найдена)");
        dbErrLabel->setStyleSheet("color: #cf5b52; font-size: 13px;");
        dbErrLayout->addWidget(dbErrLabel);
        dbErrLayout->setAlignment(Qt::AlignCenter);
        centralStack_->insertWidget(ModeDb, dbError);

        // Продолжаем работу — приложение остаётся рабочим в части Сбор/Детально
        return;
    }

    // --- Создаём виджеты ---
    configDockWidget_ = new ConfigManagerWidget(dbProvider_.get(), this);
    paramDockWidget_ = new ParameterBrowser(dbProvider_.get(), this);
    watchSetDockWidget_ = new WatchSetWidget(dbProvider_.get(), this);

    // --- Док-виджеты ---
    configDock_ = new QDockWidget("Конфигурации", this);
    configDock_->setWidget(configDockWidget_);
    configDock_->setMinimumWidth(280);
    addDockWidget(Qt::LeftDockWidgetArea, configDock_);

    paramDock_ = new QDockWidget("Библиотека параметров", this);
    paramDock_->setWidget(paramDockWidget_);
    paramDock_->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, paramDock_);

    watchSetDock_ = new QDockWidget("Активный набор", this);
    watchSetDock_->setWidget(watchSetDockWidget_);
    watchSetDock_->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, watchSetDock_);

    // --- Лог как док-виджет (уже создан, но добавим как док, чтобы его можно было скрыть) ---
    logDock_ = new QDockWidget("Лог", this);
    logDock_->setWidget(logEdit_);
    logDock_->setMaximumHeight(150);
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);

    // --- Подключаем сигналы ---
    connect(configDockWidget_, &ConfigManagerWidget::applyConfigRequested,
            this, &MainWindow::applyConfiguration);
    connect(configDockWidget_, &ConfigManagerWidget::refreshMetadataRequested,
            this, &MainWindow::onRefreshMetadata);

    connect(paramDockWidget_, &ParameterBrowser::parametersSelected,
            [this](const QList<QString>& addresses, const QList<QString>& names) {
                std::vector<orbita::ChannelSpec> specs;
                for (int i = 0; i < addresses.size(); ++i) {
                    std::string norm = encoding::normalizeAddress(addresses[i].toStdString());
                    if (norm.empty()) continue;
                    // Категорию возьмём из БД
                    QString cat = dbProvider_ ? dbProvider_->getCategory(addresses[i]) : QString();
                    specs.push_back({norm, names[i].toStdString(), cat.toStdString()});
                }
                if (watchSetDockWidget_)
                    watchSetDockWidget_->addParams(specs);
            });

    connect(paramDockWidget_, &ParameterBrowser::overrideTolerance,
            [this](QString address, double lo, double nominal, double hi) {
                toleranceResolver_.setOverride(address, chstatus::Tolerance{lo, nominal, hi, true});
            });
    connect(paramDockWidget_, &ParameterBrowser::toleranceSavedToDb,
            [this](QString address) { toleranceResolver_.clearOverride(address); });

    connect(watchSetDockWidget_, &WatchSetWidget::watchSetChanged,
            this, &MainWindow::onWatchSetChanged);
    connect(watchSetDockWidget_, &WatchSetWidget::configSaved,
            [this]() { if (configDockWidget_) configDockWidget_->refreshFileList(); });

    // --- Заменяем страницы Конфиг и БД в центральном стеке на реальные виджеты ---
    // Удаляем временные placeholder'ы
    QWidget* oldConfig = centralStack_->widget(ModeConfig);
    QWidget* oldDb = centralStack_->widget(ModeDb);
    centralStack_->removeWidget(oldConfig);
    centralStack_->removeWidget(oldDb);
    delete oldConfig;
    delete oldDb;

    // Создаём новые страницы с реальными виджетами
    // Для Конфига используем тот же виджет, что и в доке, но он не может быть одновременно в двух местах.
    // Поэтому создадим отдельный экземпляр для страницы.
    configPage_ = new ConfigManagerWidget(dbProvider_.get(), this);
    centralStack_->insertWidget(ModeConfig, configPage_);

    dbPage_ = new ParameterBrowser(dbProvider_.get(), this);
    centralStack_->insertWidget(ModeDb, dbPage_);

    // Подключаем сигналы страниц (если нужны)
    connect(configPage_, &ConfigManagerWidget::applyConfigRequested,
            this, &MainWindow::applyConfiguration);
    connect(configPage_, &ConfigManagerWidget::refreshMetadataRequested,
            this, &MainWindow::onRefreshMetadata);

    connect(dbPage_, &ParameterBrowser::parametersSelected,
            [this](const QList<QString>& addresses, const QList<QString>& names) {
                // Аналогично добавлению из док-виджета
                std::vector<orbita::ChannelSpec> specs;
                for (int i = 0; i < addresses.size(); ++i) {
                    std::string norm = encoding::normalizeAddress(addresses[i].toStdString());
                    if (norm.empty()) continue;
                    QString cat = dbProvider_ ? dbProvider_->getCategory(addresses[i]) : QString();
                    specs.push_back({norm, names[i].toStdString(), cat.toStdString()});
                }
                if (watchSetDockWidget_)
                    watchSetDockWidget_->addParams(specs);
            });

    connect(dbPage_, &ParameterBrowser::overrideTolerance,
            [this](QString address, double lo, double nominal, double hi) {
                toleranceResolver_.setOverride(address, chstatus::Tolerance{lo, nominal, hi, true});
            });
    connect(dbPage_, &ParameterBrowser::toleranceSavedToDb,
            [this](QString address) { toleranceResolver_.clearOverride(address); });

    // Передаём БД в MainPage и DetailView
    mainPage_->setMetadataService(dbProvider_.get());
    detailView_->setMetadataService(dbProvider_.get());

    // Инициализируем ToleranceResolver и передаём в MainPage и DetailView
    toleranceResolver_.setDb(dbProvider_.get());
    {
        int tolN = toleranceResolver_.loadConfigFile(
            QApplication::applicationDirPath() + "/address/tolerances.tol");
        if (tolN < 0)
            log("tolerances.tol не найден — допуски только из БД");
        else
            log(QString("Загружено %1 допусков из tolerances.tol").arg(tolN));
    }
    mainPage_->setToleranceResolver(&toleranceResolver_);
    detailView_->setToleranceResolver(&toleranceResolver_);

    // Доки скрыты по умолчанию — экран Сбор чистый. Вызвать можно через меню «Вид».
    configDock_->hide();
    paramDock_->hide();
    watchSetDock_->hide();

    QMenu* viewMenu = menuBar()->addMenu("Вид");
    viewMenu->addAction(watchSetDock_->toggleViewAction());
    viewMenu->addAction(configDock_->toggleViewAction());
    viewMenu->addAction(paramDock_->toggleViewAction());
    viewMenu->addAction(logDock_->toggleViewAction());

    // Устанавливаем начальный набор (пустой)
    onWatchSetChanged({});
}

void MainWindow::setupToolBar()
{
    QToolBar* toolbar = addToolBar("Управление");
    toolbar->setMovable(false);
    toolbar->setStyleSheet("QToolBar { spacing: 4px; }");

    // --- Группа переключения режимов ---
    QActionGroup* modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);

    actMain_   = toolbar->addAction(QIcon(":/icons/collect.svg"),  "Сбор");
    actDetail_ = toolbar->addAction(QIcon(":/icons/detail.svg"),   "Детально");
    actConfig_ = toolbar->addAction(QIcon(":/icons/config.svg"),   "Конфиг");
    actDb_     = toolbar->addAction(QIcon(":/icons/database.svg"), "БД");

    actMain_->setCheckable(true);
    actDetail_->setCheckable(true);
    actConfig_->setCheckable(true);
    actDb_->setCheckable(true);
    actMain_->setChecked(true);

    modeGroup->addAction(actMain_);
    modeGroup->addAction(actDetail_);
    modeGroup->addAction(actConfig_);
    modeGroup->addAction(actDb_);

    connect(actMain_, &QAction::triggered, [this]() { setMode(ModeMain); });
    connect(actDetail_, &QAction::triggered, [this]() { setMode(ModeDetail); });
    connect(actConfig_, &QAction::triggered, [this]() { setMode(ModeConfig); });
    connect(actDb_, &QAction::triggered, [this]() { setMode(ModeDb); });

    toolbar->addSeparator();

    // --- Быстрый выбор конфига ---
    configCombo_ = new QComboBox;
    configCombo_->setFixedWidth(190);
    configCombo_->setStyleSheet(
        "QComboBox { background:#1B1F26; color:#aab4c0; border:1px solid #2a313b;"
        "            border-radius:3px; padding:2px 6px; }"
        "QComboBox::drop-down { border:none; width:18px; }"
        "QComboBox QAbstractItemView { background:#1B1F26; color:#dfe6ee; selection-background-color:#2a3345; }");
    configCombo_->setToolTip("Быстрый выбор конфигурации (применяется сразу)");
    refreshConfigCombo();
    toolbar->addWidget(configCombo_);

    toolbar->addSeparator();

    // --- Кнопки Старт/Стоп ---
    startBtn_ = new QPushButton(QIcon(":/icons/play.svg"), " Старт");
    stopBtn_  = new QPushButton(QIcon(":/icons/stop.svg"),  " Стоп");
    startBtn_->setEnabled(false);
    stopBtn_->setEnabled(false);
    startBtn_->setFixedWidth(80);
    stopBtn_->setFixedWidth(80);
    toolbar->addWidget(startBtn_);
    toolbar->addWidget(stopBtn_);

    toolbar->addSeparator();

    // --- Запись ---
    recordBtn_ = new QPushButton(QIcon(":/icons/record.svg"), " Запись");
    recordBtn_->setEnabled(false);
    recordBtn_->setFixedWidth(90);
    toolbar->addWidget(recordBtn_);

    recordingLabel_ = new QLabel("—");
    recordingLabel_->setMinimumWidth(150);
    recordingLabel_->setStyleSheet("color: #7e8a98; font-size: 11px;");
    toolbar->addWidget(recordingLabel_);

    toolbar->addSeparator();

    // --- Инверт ---
    invertCheck_ = new QCheckBox("Инверт.");
    invertCheck_->setStyleSheet("QCheckBox { color: #aab4c0; }");
    toolbar->addWidget(invertCheck_);

    toolbar->addSeparator();

    // --- МТВ и статус ---
    mtvLabel_ = new QLabel("--:--:--");
    mtvLabel_->setFont(QFont("Courier New", 14, QFont::Bold));
    mtvLabel_->setStyleSheet("color: #dfe6ee;");
    toolbar->addWidget(mtvLabel_);

    statusLabel_ = new QLabel("● Сбор остановлен");
    statusLabel_->setStyleSheet("color: #d99a4a; font-weight: 500;");
    toolbar->addWidget(statusLabel_);

    toolbar->addSeparator();

    // --- Ошибки ---
    errPhraseLabel_ = new QLabel("ошибки фраз: 0%");
    errGroupLabel_ = new QLabel("групп: 0%");
    errPhraseLabel_->setStyleSheet("color: #8b95a3; font-size: 11px; font-family: 'IBM Plex Mono';");
    errGroupLabel_->setStyleSheet("color: #8b95a3; font-size: 11px; font-family: 'IBM Plex Mono';");
    toolbar->addWidget(errPhraseLabel_);
    toolbar->addWidget(errGroupLabel_);

    // --- Подключаем сигналы кнопок ---
    connect(startBtn_, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(stopBtn_, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(recordBtn_, &QPushButton::clicked, this, &MainWindow::onToggleRecording);
    connect(invertCheck_, &QCheckBox::toggled,
            [this](bool v){ orbita_->setInvertSignal(v); });

    // --- Подключаем сигналы DetailView ---
    connect(detailView_, &DetailView::backToMain, [this]() { setMode(ModeMain); });
    connect(detailView_, &DetailView::navigateChannel, this, &MainWindow::onNavigateChannel);

    // --- Подключаем сигналы MainPage ---
    connect(mainPage_, &MainPage::channelSelected, this, &MainWindow::onChannelSelected);
    connect(mainPage_, &MainPage::channelDoubleClicked, this, &MainWindow::onChannelDoubleClicked);

    // --- Действия для док-виджетов (показывать/скрывать) ---
    // Добавим переключатели видимости доков в меню View (можно позже)

    // --- Кнопка «Сценарий проверки» ---
    toolbar->addSeparator();
    actScenario_ = toolbar->addAction(QIcon(":/icons/scenario.svg"), "Сценарий");
    connect(actScenario_, &QAction::triggered, this, &MainWindow::onOpenScenario);

    // --- Подключаем config combo ---
    connect(configCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (i > 0) applyConfiguration(configCombo_->itemText(i));
    });
}

// ----------------------------------------------------------------------------
//  Сценарий проверки
// ----------------------------------------------------------------------------
void MainWindow::onOpenScenario()
{
    // Передаём провайдер значений как лямбду — ScenarioWizard не зависит от ядра напрямую
    ValueProvider provider = [this](const QString& addr) -> std::optional<double> {
        return orbita_->getValueByAddress(addr.toStdString());
    };

    ScenarioWizard wizard(provider, this);
    wizard.exec();
}

// ----------------------------------------------------------------------------
//  Управление режимами
// ----------------------------------------------------------------------------
void MainWindow::setMode(int mode)
{
    // Ранний выход если стек не инициализирован
    if (!centralStack_)
        return;

    centralStack_->setCurrentIndex(mode);

    // Обновляем состояние кнопок на панели (с проверками на nullptr)
    if (actMain_)
        actMain_->setChecked(mode == ModeMain);
    if (actDetail_)
        actDetail_->setChecked(mode == ModeDetail);
    if (actConfig_)
        actConfig_->setChecked(mode == ModeConfig);
    if (actDb_)
        actDb_->setChecked(mode == ModeDb);

    // Доки пользователь сам показывает/прячет через меню «Вид» — не навязываем по режиму.

    // Если перешли в детальный режим и есть выбранный канал – обновляем DetailView
    if (mode == ModeDetail && selectedChannelIndex_ >= 0) {
        const auto& specs = orbita_->getChannels();
        if (selectedChannelIndex_ < (int)specs.size()) {
            if (detailView_)
                detailView_->setChannel(specs[selectedChannelIndex_], selectedChannelIndex_);
            // Значение обновится в updateData
        }
    }
}

// ----------------------------------------------------------------------------
//  Управление сбором
// ----------------------------------------------------------------------------
void MainWindow::onStart()
{
    try {
        orbita_->start();
        elapsedTimer_.restart();
        isRunning_ = true;
        startBtn_->setEnabled(false);
        stopBtn_->setEnabled(true);
        recordBtn_->setEnabled(true);
        statusLabel_->setText("● Сбор идёт");
        statusLabel_->setStyleSheet("color: #7fc79a; font-weight: 500;");
        log("Старт сбора данных (E20-10)");
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Ошибка запуска", e.what());
        log("Ошибка запуска: " + QString::fromLocal8Bit(e.what()));
    }
}

void MainWindow::onStop()
{
    orbita_->stop();
    isRunning_ = false;
    if (isRecording_) onToggleRecording(); // остановить запись
    startBtn_->setEnabled(!currentSpecs_.empty());
    stopBtn_->setEnabled(false);
    recordBtn_->setEnabled(false);
    statusLabel_->setText("● Сбор остановлен");
    statusLabel_->setStyleSheet("color: #d99a4a; font-weight: 500;");
    log("Стоп сбора данных");
}

// ----------------------------------------------------------------------------
//  Запись TLM
// ----------------------------------------------------------------------------
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
        recordBtn_->setText("⏹ Стоп");
        recordingLabel_->setText("● " + path.section('/', -1));
        recordingLabel_->setStyleSheet("color: #cf5b52; font-weight: bold;");
        log("Запись: " + path);
    } else {
        orbita_->stopRecording();
        isRecording_ = false;
        recordBtn_->setText("⏺ Запись");
        recordingLabel_->setText("—");
        recordingLabel_->setStyleSheet("color: #7e8a98;");
        log("Запись остановлена");
    }
}

// ----------------------------------------------------------------------------
//  Строка статуса
// ----------------------------------------------------------------------------
void MainWindow::updateStatusBar(const orbita::Snapshot& snap)
{
    if (!m_statusBarLabel) return;

    // Время МТВ
    uint32_t sec = snap.mtv_seconds;
    QString timeStr = QString("%1:%2:%3")
        .arg(sec / 3600,       2, 10, QChar('0'))
        .arg((sec % 3600) / 60, 2, 10, QChar('0'))
        .arg(sec % 60,          2, 10, QChar('0'));

    // Подсчёт аномалий
    int anomalyCount = 0;
    int firstAnomalyIdx = -1;
    for (int i = 0; i < (int)snap.values.size(); ++i) {
        const auto& v = snap.values[i];
        auto tol = chstatus::forAddress(dbProvider_.get(), v.address);
        auto lvl = chstatus::evaluate(v.value, tol);
        if (chstatus::isAnomaly(lvl)) {
            if (firstAnomalyIdx < 0) firstAnomalyIdx = i;
            ++anomalyCount;
        }
    }

    QString toleranceStr = QString("контроль допусков: %1 вне нормы").arg(anomalyCount);
    if (anomalyCount > 0 && firstAnomalyIdx >= 0) {
        QString name;
        if (firstAnomalyIdx < (int)currentSpecs_.size()) {
            const auto& sp = currentSpecs_[firstAnomalyIdx];
            name = QString::fromStdString(sp.name.empty() ? sp.address : sp.name);
        } else {
            name = QString::fromStdString(snap.values[firstAnomalyIdx].address);
        }
        toleranceStr += QString(" — КАН %1 %2").arg(firstAnomalyIdx + 1).arg(name);
    }

    // Запись
    QString recStr = isRecording_ ? recordingLabel_->text() : "—";

    QString line = QString("● %1  |  %2  |  запись: %3  |  10 Гц · кадр 1024 Б")
        .arg(timeStr, toleranceStr, recStr);

    m_statusBarLabel->setText(line);
}

// ----------------------------------------------------------------------------
//  Обновление данных
// ----------------------------------------------------------------------------
void MainWindow::updateData()
{
    if (!isRunning_) {
        if (m_statusBarLabel)
            m_statusBarLabel->setText("● сбор остановлен");
        return;
    }

    if (orbita_->waitForData(std::chrono::milliseconds(0))) {
        orbita::Snapshot snap = orbita_->getSnapshot();

        // МТВ
        uint32_t sec = snap.mtv_seconds;
        mtvLabel_->setText(QString("%1:%2:%3")
                               .arg(sec / 3600, 2, 10, QChar('0'))
                               .arg((sec % 3600) / 60, 2, 10, QChar('0'))
                               .arg(sec % 60, 2, 10, QChar('0')));

        // Ошибки
        errPhraseLabel_->setText(QString("ошибки фраз: %1%").arg(snap.stats.phrase_error_percent));
        errGroupLabel_->setText(QString("групп: %1%").arg(snap.stats.group_error_percent));

        // Нижняя строка статуса
        updateStatusBar(snap);

        // Обновляем MainPage
        if (mainPage_)
            mainPage_->updateData(snap);

        // Если в режиме детального просмотра – обновляем DetailView
        if (centralStack_->currentIndex() == ModeDetail && selectedChannelIndex_ >= 0) {
            const auto& specs = orbita_->getChannels();
            if (selectedChannelIndex_ < (int)specs.size()) {
                const auto& spec = specs[selectedChannelIndex_];
                double val = 0.0;
                for (const auto& v : snap.values) {
                    if (v.address == spec.address) {
                        val = v.value;
                        break;
                    }
                }
                detailView_->updateValue(val);
            }
        }
    }
}

// ----------------------------------------------------------------------------
//  Конфигурация и метаданные
// ----------------------------------------------------------------------------
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
        std::string normAddr = encoding::normalizeAddress(line);
        if (normAddr.empty()) continue;
        QString qnorm = QString::fromStdString(normAddr);
        QString name = dbProvider_ ? dbProvider_->getName(qnorm) : QString();
        QString cat = dbProvider_ ? dbProvider_->getCategory(qnorm) : QString();
        specs.push_back({normAddr, name.toStdString(), cat.toStdString()});
    }

    if (specs.empty()) {
        log("Конфигурация пуста");
        return;
    }

    if (watchSetDockWidget_)
        watchSetDockWidget_->setFromSpecs(specs);
    log(QString("Конфигурация загружена: %1 (%2 каналов)")
            .arg(fileName).arg(specs.size()));
}

void MainWindow::refreshConfigCombo()
{
    if (!configCombo_) return;
    QString curText = configCombo_->currentIndex() > 0 ? configCombo_->currentText() : QString();
    configCombo_->blockSignals(true);
    configCombo_->clear();
    configCombo_->addItem("— конфиг —");
    QString addrDir = QCoreApplication::applicationDirPath() + "/address/";
    QDir dir(addrDir);
    for (const QString& f : dir.entryList({"*.txt"}, QDir::Files, QDir::Name))
        configCombo_->addItem(f);
    int idx = curText.isEmpty() ? -1 : configCombo_->findText(curText);
    configCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
    configCombo_->blockSignals(false);
}

void MainWindow::onRefreshMetadata()
{
    if (dbProvider_) dbProvider_->refresh();
    if (configDockWidget_) configDockWidget_->updateMetadata();
    if (configPage_) configPage_->updateMetadata();
    if (paramDockWidget_) paramDockWidget_->rebuildTree();
    if (dbPage_) dbPage_->rebuildTree();
    refreshConfigCombo();
    log("Метаданные обновлены");
}

// ----------------------------------------------------------------------------
//  Активный набор
// ----------------------------------------------------------------------------
void MainWindow::onWatchSetChanged(const std::vector<orbita::ChannelSpec>& specs)
{
    try {
        orbita_->setChannels(specs);
    } catch (const std::exception& e) {
        log("Ошибка набора: " + QString::fromLocal8Bit(e.what()));
        return;
    }

    currentSpecs_ = specs;
    if (mainPage_)
        mainPage_->setChannels(specs);

    startBtn_->setEnabled(!specs.empty() && !isRunning_);
    stopBtn_->setEnabled(isRunning_);
    recordBtn_->setEnabled(isRunning_);

    // Если выбранный индекс выходит за пределы, сбрасываем
    if (selectedChannelIndex_ >= (int)specs.size())
        selectedChannelIndex_ = -1;
    // Авто-выбор первого канала, чтобы отсчёт не висел на "---"
    if (selectedChannelIndex_ < 0 && !specs.empty())
        selectedChannelIndex_ = 0;
    if (selectedChannelIndex_ >= 0 && !specs.empty())
        onChannelSelected(selectedChannelIndex_);
}

// ----------------------------------------------------------------------------
//  Выбор канала
// ----------------------------------------------------------------------------
void MainWindow::onChannelSelected(int index)
{
    selectedChannelIndex_ = index;
    if (mainPage_)
        mainPage_->setSelectedChannel(index);

    if (centralStack_->currentIndex() == ModeDetail) {
        const auto& specs = orbita_->getChannels();
        if (index >= 0 && index < (int)specs.size()) {
            detailView_->setChannel(specs[index], index);
            // Значение будет обновлено в updateData
        }
    }
}

void MainWindow::onChannelDoubleClicked(int index)
{
    setMode(ModeDetail);
    onChannelSelected(index);
}

void MainWindow::onNavigateChannel(int delta)
{
    int newIdx = selectedChannelIndex_ + delta;
    const auto& specs = orbita_->getChannels();
    if (newIdx >= 0 && newIdx < (int)specs.size()) {
        onChannelSelected(newIdx);
    }
}

// ----------------------------------------------------------------------------
//  Вспомогательные методы
// ----------------------------------------------------------------------------
void MainWindow::log(const QString& msg)
{
    QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    if (logEdit_) {
        logEdit_->append(QString("[%1] %2").arg(ts, msg));
        logEdit_->moveCursor(QTextCursor::End);
    }
    qDebug() << msg;
}

// ----------------------------------------------------------------------------
//  Извлечение номера канала из адреса (пример: "M16P1A70B12C10D10T01" -> ?)
//  В прототипе номер канала – это порядковый индекс в конфигурации, а не из адреса.
//  Поэтому мы используем индекс, переданный из виджетов.
//  Оставим этот метод как заглушку, если понадобится.
// ----------------------------------------------------------------------------
int MainWindow::extractChannelNumber(const std::string& address)
{
    // Пока не используется, но можно реализовать парсинг, если нужно
    return -1;
}
